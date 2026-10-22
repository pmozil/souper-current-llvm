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

#include "souper/Inst/MemCost.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

using namespace souper;

int souper::memOpCost(const Inst *I, const MemCostContext &Ctx) {
  if (Ctx.OpCostHook)
    if (std::optional<int> C = Ctx.OpCostHook(I))
      return *C * Ctx.OpCostScale;
  if (I->K != Inst::Load && I->K != Inst::Store)
    return 0;
  int Cost = I->K == Inst::Load ? MemCostDefaults::LoadBase
                                : MemCostDefaults::StoreBase;
  unsigned Bits = I->K == Inst::Load ? I->Width : I->Ops[2]->Width;
  unsigned Bytes = (Bits + 7) / 8;
  if (Bytes && I->MA.Align < Bytes)
    Cost += 1;                       // under-aligned access
  return Cost;
}

// Loads that `Operand`'s expression reads directly (not through other loads).
static void loadsUsedBy(Inst *Operand, std::set<Inst *> &Out,
                        std::set<Inst *> &Seen) {
  if (!Seen.insert(Operand).second)
    return;
  if (Operand->K == Inst::Load) {
    Out.insert(Operand);
    return;
  }
  for (Inst *Op : Operand->Ops)
    loadsUsedBy(Op, Out, Seen);
}

// base + constant byte offset of a pointer expression.
static void splitPtr(Inst *P, Inst *&Base, int64_t &Off) {
  Off = 0;
  while ((P->K == Inst::PtrAdd || P->K == Inst::PtrAddInBounds) &&
         P->Ops[1]->K == Inst::Const && P->Ops[1]->Width <= 64) {
    Off += P->Ops[1]->Val.getSExtValue();
    P = P->Ops[0];
  }
  Base = P;
}

int souper::memScheduleCost(const MemSchedule &S, const MemCostContext &Ctx) {
  int Total = 0;
  for (Inst *Op : S.Ops)
    Total += memOpCost(Op, Ctx);

  // (1) Load-to-use distance. A load's result is first needed by the first
  //     later op whose operands read it, else by the end of the region.
  std::vector<std::set<Inst *>> Reads(S.Ops.size());
  for (unsigned J = 0; J < S.Ops.size(); ++J) {
    std::set<Inst *> Seen;
    Inst *Op = S.Ops[J];
    loadsUsedBy(Op->Ops[1], Reads[J], Seen);
    if (Op->K == Inst::Store)
      loadsUsedBy(Op->Ops[2], Reads[J], Seen);
  }
  for (unsigned I = 0; I < S.Ops.size(); ++I) {
    if (S.Ops[I]->K != Inst::Load)
      continue;
    unsigned Use = S.Ops.size();
    for (unsigned J = I + 1; J < S.Ops.size(); ++J)
      if (Reads[J].count(S.Ops[I])) {
        Use = J;
        break;
      }
    unsigned Dist = Use - I;
    if (Dist < Ctx.LoadLatency)
      Total += (Ctx.LoadLatency - Dist) * Ctx.UseStallPenalty;
  }

  // (2) Adjacent accesses to consecutive addresses can be combined.
  for (unsigned I = 0; I + 1 < S.Ops.size(); ++I) {
    Inst *A = S.Ops[I], *B = S.Ops[I + 1];
    if (A->K != B->K)
      continue;
    unsigned WA = A->K == Inst::Load ? A->Width : A->Ops[2]->Width;
    unsigned WB = B->K == Inst::Load ? B->Width : B->Ops[2]->Width;
    if (WA != WB)
      continue;
    Inst *BaseA, *BaseB;
    int64_t OffA, OffB;
    splitPtr(A->Ops[1], BaseA, OffA);
    splitPtr(B->Ops[1], BaseB, OffB);
    int64_t Bytes = (WA + 7) / 8;
    if (BaseA == BaseB && (OffB - OffA == Bytes || OffA - OffB == Bytes))
      Total -= Ctx.AdjacentBonus;
  }

  if (Ctx.ExtraScheduleCost)
    Total += Ctx.ExtraScheduleCost(S);
  return Total;
}

void souper::calibrateMemCost(MemCostContext &Ctx, int SourceLoadCost) {
  if (SourceLoadCost <= 0)
    return;
  const int Base = MemCostDefaults::LoadBase;
  if (SourceLoadCost <= Base) {
    Ctx.OpCostScale = std::max(1, (Base + SourceLoadCost / 2) / SourceLoadCost);
  } else {
    int Ratio = std::max(1, (SourceLoadCost + Base / 2) / Base);
    Ctx.UseStallPenalty = MemCostDefaults::UseStallPenalty * Ratio;
    Ctx.AdjacentBonus = MemCostDefaults::AdjacentBonus * Ratio;
  }
}

int souper::memCost(Inst *Root, const MemCostContext &Ctx) {
  MemSchedule S = linearize(Root);
  if (!S.Valid)
    return std::numeric_limits<int>::max() / 4;
  return memScheduleCost(S, Ctx);
}

int souper::memBenefit(Inst *LHS, Inst *RHS, const MemCostContext &Ctx) {
  return memCost(LHS, Ctx) - memCost(RHS, Ctx);
}
