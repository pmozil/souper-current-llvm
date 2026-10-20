// Copyright 2026 The Souper Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "souper/Infer/MemorySynthesis.h"
#include "souper/Infer/AliveDriver.h"
#include "souper/Infer/MemInterpreter.h"
#include "souper/Inst/MemSchedule.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

using namespace souper;

namespace {

using DepList = std::vector<std::vector<unsigned>>;

// What we know about one memory op of the region, taken from the original DAG.
struct OpInfo {
  bool IsLoad = false;
  Inst *Ptr = nullptr;
  unsigned Bits = 0;
  bool Known = false;
};

// Moves applied on top of pure reordering.
struct Moves {
  std::map<unsigned, unsigned> Forward;
  std::set<unsigned> DeadStores;
  bool empty() const { return Forward.empty() && DeadStores.empty(); }
  bool removed(unsigned N) const {
    return Forward.count(N) || DeadStores.count(N);
  }
};

// Dependences of the active nodes only.
// A removed node D is replaced, wherever
// it appears, by D's own dependences
// (so ordering stays transitive), and a
// forwarded load additionally by the store that
// supplies its value (its users
// now read that value,
// which only exists once the store has been built).
DepList reduceDeps(const DepList &Deps, const Moves &M) {
  DepList Out(Deps.size());
  for (unsigned J = 0; J < Deps.size(); ++J) {
    if (M.removed(J))
      continue;
    std::set<unsigned> Seen, Result;
    std::vector<unsigned> Work(Deps[J].begin(), Deps[J].end());
    while (!Work.empty()) {
      unsigned D = Work.back();
      Work.pop_back();
      if (!Seen.insert(D).second)
        continue;
      if (!M.removed(D)) {
        Result.insert(D);
        continue;
      }
      for (unsigned X : Deps[D])
        Work.push_back(X);
      auto F = M.Forward.find(D);
      if (F != M.Forward.end())
        Work.push_back(F->second);
    }
    Out[J].assign(Result.begin(), Result.end());
  }
  return Out;
}

// Bounded depth-first enumeration
// of topological orders. The first order
// produced is the original one.
void enumerateOrders(const DepList &Deps, const std::vector<unsigned> &Nodes,
                     unsigned Cap, std::vector<std::vector<unsigned>> &Out) {
  std::set<unsigned> Active(Nodes.begin(), Nodes.end()), Placed;
  std::vector<unsigned> Cur;
  std::function<void()> Rec = [&]() {
    if (Out.size() >= Cap)
      return;
    if (Cur.size() == Nodes.size()) {
      Out.push_back(Cur);
      return;
    }
    for (unsigned N : Nodes) {
      if (Placed.count(N))
        continue;
      bool Ready = true;
      for (unsigned D : Deps[N])
        if (Active.count(D) && !Placed.count(D)) {
          Ready = false;
          break;
        }
      if (!Ready)
        continue;
      Placed.insert(N);
      Cur.push_back(N);
      Rec();
      Cur.pop_back();
      Placed.erase(N);
    }
  };
  Rec();
}

// One order that hoists loads as early as the dependences allow.
// It maximises load-to-use distance in a single shot,
// which keeps big windows from being searched only
// in their lexicographically first corner.
std::vector<unsigned> greedyHoist(const DepList &Deps,
                                  const std::vector<unsigned> &Nodes,
                                  const std::vector<OpInfo> &Info) {
  std::set<unsigned> Active(Nodes.begin(), Nodes.end()), Placed;
  std::vector<unsigned> Order;
  while (Order.size() < Nodes.size()) {
    int Pick = -1;
    for (unsigned N : Nodes) {
      if (Placed.count(N))
        continue;
      bool Ready = true;
      for (unsigned D : Deps[N])
        if (Active.count(D) && !Placed.count(D)) {
          Ready = false;
          break;
        }
      if (!Ready)
        continue;
      if (Pick < 0 || (Info[N].IsLoad && !Info[(unsigned)Pick].IsLoad))
        Pick = (int)N;
    }
    if (Pick < 0)
      break;                     // cycle: cannot happen for a valid Deps
    Placed.insert((unsigned)Pick);
    Order.push_back((unsigned)Pick);
  }
  return Order;
}

} // namespace

std::error_code souper::synthesizeMemoryReorders(
    MemoryRegionExtractor &X, const MemoryRegionCandidate &C, InstContext &IC,
    const MemSynthOptions &O, std::vector<MemPlan> &Verified,
    MemSynthStats *Stats) {
  Verified.clear();
  MemSynthStats Local;
  MemSynthStats &St = Stats ? *Stats : Local;

  Inst *Orig = C.Mapping.LHS;
  MemSchedule OrigSched = linearize(Orig);
  if (!OrigSched.Valid)
    return std::make_error_code(std::errc::invalid_argument);
  const int OrigCost = memCost(Orig, O.Cost);
  const unsigned N = C.MemOps.size();

  // 1. Facts about each op, via the LLVM instruction recorded as an origin.
  std::vector<OpInfo> Info(N);
  for (Inst *Op : OrigSched.Ops) {
    for (unsigned I = 0; I < N; ++I) {
      if (!Op->hasOrigin(C.MemOps[I]))
        continue;
      Info[I].IsLoad = Op->K == Inst::Load;
      Info[I].Ptr = Op->Ops[1];
      Info[I].Bits = Op->K == Inst::Load ? Op->Width : Op->Ops[2]->Width;
      Info[I].Known = true;
    }
  }
  for (const OpInfo &I : Info)
    if (!I.Known)
      return std::make_error_code(std::errc::invalid_argument);

  auto DepOn = [&](unsigned J, unsigned I) {
    return std::find(C.Deps[J].begin(), C.Deps[J].end(), I) != C.Deps[J].end();
  };

  // 2. Store-to-load forwarding.
  // Scan downwards from each load. The nearest
  // store to the same address and
  // width supplies the value, unless a store in
  // between may touch the load's location first.
  Moves M;
  for (unsigned L = 0; L < N; ++L) {
    if (!Info[L].IsLoad)
      continue;
    for (int S = (int)L - 1; S >= 0; --S) {
      if (Info[S].IsLoad)
        continue;
      if (Info[S].Ptr == Info[L].Ptr && Info[S].Bits == Info[L].Bits) {
        M.Forward[L] = (unsigned)S;
        break;
      }
      if (DepOn(L, (unsigned)S))
        break;                   // clobbered (or only partially overlapped)
    }
  }

  // 3. Dead stores, judged after forwarding.
  // A store is dead if a later store
  // to the same address is at least as wide
  // and no remaining load between them may read it.
  for (unsigned S = 0; S < N; ++S) {
    if (Info[S].IsLoad)
      continue;
    for (unsigned T = S + 1; T < N; ++T) {
      if (Info[T].IsLoad || Info[T].Ptr != Info[S].Ptr ||
          Info[T].Bits < Info[S].Bits)
        continue;
      bool Read = false;
      for (unsigned K = S + 1; K < T; ++K)
        if (Info[K].IsLoad && !M.Forward.count(K) && DepOn(K, S))
          Read = true;
      if (!Read) {
        M.DeadStores.insert(S);
        break;
      }
    }
  }

  // 4. Candidate plans, pure reorders, then the same with the moves applied.
  struct Plan {
    MemPlan P;
    Inst *RHS = nullptr;
    int Cost = 0;
    unsigned Seq = 0;
  };
  std::vector<Plan> Plans;
  std::set<Inst *> SeenRoots{Orig};
  unsigned Seq = 0;

  auto Consider = [&](const std::vector<unsigned> &Order, const Moves &Mv) {
    MemPlan P;
    P.Order = Order;
    P.Forward = Mv.Forward;
    P.DeadStores = Mv.DeadStores;
    Inst *RHS = X.build(C, P);
    if (!RHS)
      return;
    ++St.PlansBuilt;
    if (!SeenRoots.insert(RHS).second)
      return;                    // same DAG as one already seen (uniqued)
    int Cost = memCost(RHS, O.Cost);
    if (Cost >= OrigCost)
      return;                    // strict improvement only (guarantees termination)
    ++St.Cheaper;
    Plans.push_back({std::move(P), RHS, Cost, Seq++});
  };

  auto Explore = [&](const Moves &Mv) {
    DepList Deps = reduceDeps(C.Deps, Mv);
    std::vector<unsigned> Nodes;
    for (unsigned I = 0; I < N; ++I)
      if (!Mv.removed(I))
        Nodes.push_back(I);
    if (Nodes.empty())
      return;
    std::vector<unsigned> G = greedyHoist(Deps, Nodes, Info);
    if (G.size() == Nodes.size())
      Consider(G, Mv);
    std::vector<std::vector<unsigned>> Orders;
    enumerateOrders(Deps, Nodes, O.MaxOrders, Orders);
    for (auto &Ord : Orders)
      Consider(Ord, Mv);
  };

  Explore(Moves());
  if (!M.empty())
    Explore(M);

  // 5. Cheapest first, ties keep generation order, so the result is deterministic.
  std::stable_sort(Plans.begin(), Plans.end(), [](const Plan &A, const Plan &B) {
    return A.Cost != B.Cost ? A.Cost < B.Cost : A.Seq < B.Seq;
  });

  // 6. Prune with the concrete interpreter,
  // then ask Alive2 in cost order. The
  // first plan it accepts is the cheapest valid one.
  unsigned Calls = 0;
  for (Plan &Pl : Plans) {
    if (Calls >= O.MaxVerify)
      break;
    if (!memTrialsAgree(Orig, Pl.RHS, O.PruneTrials, 1, &C.PCs)) {
      ++St.Pruned;
      continue;
    }
    ++Calls;
    ++St.Verified;
    // A timeout says nothing about validity, and the
    // solver's time depends on incidental input names,
    // so retry under a different naming scheme. A real
    // counterexample is never retried.
    // Salts are ordered by how often they solved the queries measured.
    static const unsigned Salts[] = {2, 4, 7, 0, 1, 3, 5, 6};
    bool Valid = false;
    for (unsigned Try = 0; Try < O.AliveAttempts; ++Try) {
      AliveMemNameSalt = Salts[Try % 8];
      if (Try)
        ++St.Retries;
      Valid = isTransformationValid(Orig, Pl.RHS, C.PCs, C.BPCs, IC);
      if (Valid || !AliveLastTimedOut)
        break;
    }
    AliveMemNameSalt = 0;
    if (Valid) {
      Verified.push_back(Pl.P);
      break;
    }
  }
  return std::error_code();
}
