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

#include "souper/Infer/MemInterpreter.h"
#include <algorithm>
#include <random>
#include <set>

using namespace souper;

static uint8_t initByte(uint64_t Seed, uint64_t Addr) {
  uint64_t X = Seed ^ (Addr * 0x9E3779B97F4A7C15ULL);
  X ^= X >> 29;
  X *= 0xBF58476D1CE4E5B9ULL;
  X ^= X >> 32;
  return (uint8_t)X;
}

uint8_t MemRunResult::read(uint64_t A) const {
  auto It = Written.find(A);
  return It != Written.end() ? It->second : initByte(Seed, A);
}

bool souper::runMemSchedule(const MemSchedule &S, const MemTrial &T,
                            MemRunResult &R) {
  R = MemRunResult();
  R.Seed = T.Seed;
  ValueCache Vals;
  for (auto &KV : T.VarVals)
    Vals[const_cast<Inst *>(KV.first)] = EvalValue(KV.second);

  // Loaded values are seeded into Vals,
  // so the interpreter's recursion stops
  // at them and never walks into a memory token.
  auto Eval = [&](Inst *I, llvm::APInt &Out) {
    ConcreteInterpreter CI(Vals);
    EvalValue E = CI.evaluateInst(I);
    if (!E.hasValue())
      return false;
    Out = E.getValue();
    return true;
  };

  for (Inst *Op : S.Ops) {
    llvm::APInt P;
    if (!Eval(Op->Ops[1], P))
      return false;
    uint64_t Addr = P.getZExtValue();
    unsigned Bits = Op->K == Inst::Load ? Op->Width : Op->Ops[2]->Width;
    unsigned Bytes = (Bits + 7) / 8;
    if (Op->K == Inst::Load) {
      llvm::APInt V(Bits, 0);                  // little endian
      for (unsigned B = 0; B < Bytes; ++B)
        V |= llvm::APInt(64, R.read(Addr + B)).zextOrTrunc(Bits).shl(8 * B);
      Vals[Op] = EvalValue(V);
    } else {
      llvm::APInt Val;
      if (!Eval(Op->Ops[2], Val))
        return false;
      llvm::APInt Wide = Val.zext(Bytes * 8);
      for (unsigned B = 0; B < Bytes; ++B)
        R.Written[Addr + B] =
            (uint8_t)Wide.lshr(8 * B).trunc(8).getZExtValue();
    }
  }
  for (Inst *L : S.LiveOuts) {
    llvm::APInt V;
    if (!Eval(L, V))
      return false;
    R.LiveOuts.push_back(V);
  }
  return true;
}

// Trial 0, all disjoint.
// 1, everything that may alias does.
// 2, random aliasing pairs.
// 3, partial overlap. Pointers marked noalias are always kept disjoint
// from everything, and every base honours its pointer's alignment.
static void assignLayout(const std::vector<Inst *> &Vars, unsigned Trial,
                         std::mt19937_64 &Rng, MemTrial &T) {
  std::vector<Inst *> Ptrs;
  for (Inst *V : Vars) {
    if (V->IsPtr)
      Ptrs.push_back(V);
    else
      T.VarVals[V] = llvm::APInt(64, Rng()).zextOrTrunc(V->Width);
  }
  uint64_t Next = 0x10000;
  uint64_t Shared = 0;
  for (unsigned I = 0; I < Ptrs.size(); ++I) {
    Inst *P = Ptrs[I];
    uint64_t Align = std::max<uint32_t>(P->PF.Align, 1);
    uint64_t Base = Next;
    Next += 0x1000;
    bool MayAlias = !P->PF.NoAlias;
    bool PrevMayAlias = I > 0 && !Ptrs[I - 1]->PF.NoAlias;
    switch (Trial % 4) {
    case 1:
      if (MayAlias) {
        if (!Shared)
          Shared = Base;
        Base = Shared;
      }
      break;
    case 2:
      if (MayAlias && PrevMayAlias && Rng() % 2)
        Base = T.VarVals[Ptrs[I - 1]].getZExtValue();
      break;
    case 3:
      if (MayAlias && PrevMayAlias)
        Base = T.VarVals[Ptrs[I - 1]].getZExtValue() + Align * (1 + Rng() % 3);
      break;
    default:
      break;
    }
    Base -= Base % Align;
    T.VarVals[P] = llvm::APInt(P->Width, Base);
  }
}

// Does every path condition hold for this trial?
static bool pcsHold(const std::vector<InstMapping> &PCs, const MemTrial &T) {
  ValueCache Vals;
  for (auto &KV : T.VarVals)
    Vals[const_cast<Inst *>(KV.first)] = EvalValue(KV.second);
  for (const InstMapping &PC : PCs) {
    ConcreteInterpreter CL(Vals), CR(Vals);
    EvalValue A = CL.evaluateInst(PC.LHS), B = CR.evaluateInst(PC.RHS);
    if (!A.hasValue() || !B.hasValue() || A.getValue() != B.getValue())
      return false;
  }
  return true;
}

bool souper::memTrialsAgree(Inst *L, Inst *R, unsigned N, uint64_t Seed,
                            const std::vector<InstMapping> *PCs) {
  MemSchedule SL = linearize(L), SR = linearize(R);
  if (!SL.Valid || !SR.Valid)
    return true;                       // cannot judge: let Alive2 decide

  std::vector<Inst *> Raw, Vars;
  findVars(L, Raw);
  findVars(R, Raw);
  if (PCs)
    for (const InstMapping &PC : *PCs) {
      findVars(PC.LHS, Raw);
      findVars(PC.RHS, Raw);
    }
  std::set<Inst *> Dedup;
  for (Inst *V : Raw)
    if (Dedup.insert(V).second)
      Vars.push_back(V);

  std::mt19937_64 Rng(Seed);
  for (unsigned Trial = 0; Trial < N; ++Trial) {
    MemTrial T;
    T.Seed = Rng();
    assignLayout(Vars, Trial, Rng, T);
    if (PCs && !pcsHold(*PCs, T))
      continue;                        // this layout is ruled out by the PCs
    MemRunResult A, B;
    if (!runMemSchedule(SL, T, A) || !runMemSchedule(SR, T, B))
      continue;
    if (A.LiveOuts != B.LiveOuts)
      return false;
    std::set<uint64_t> Addrs;
    for (auto &KV : A.Written)
      Addrs.insert(KV.first);
    for (auto &KV : B.Written)
      Addrs.insert(KV.first);
    for (uint64_t Addr : Addrs)
      if (A.read(Addr) != B.read(Addr))
        return false;
  }
  return true;
}
