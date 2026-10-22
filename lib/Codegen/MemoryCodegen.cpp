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

#include "souper/Codegen/Codegen.h"
#include "llvm/IR/Instructions.h"
#include <functional>
#include <map>
#include <set>

using namespace llvm;

bool souper::applyMemoryPlan(const MemoryRegionCandidate &C, const MemPlan &P) {
  std::set<Instruction *> InWin(C.Window.begin(), C.Window.end());

  // Which instructions disappear, and which loads take a stored value.
  std::set<Instruction *> Dropped;
  std::map<Value *, Value *> Fwd; // forwarded load -> stored value
  for (unsigned S : P.DeadStores)
    Dropped.insert(C.MemOps[S]);
  for (auto &KV : P.Forward) {
    auto *LI = cast<LoadInst>(C.MemOps[KV.first]);
    auto *SI = cast<StoreInst>(C.MemOps[KV.second]);
    if (LI->getType() != SI->getValueOperand()->getType())
      return false;
    Fwd[LI] = SI->getValueOperand();
    Dropped.insert(LI);
  }

  std::map<Value *, Value *> Final;
  for (auto &KV : Fwd) {
    Value *V = KV.second;
    std::set<Value *> Seen{KV.first};
    for (auto It = Fwd.find(V); It != Fwd.end(); It = Fwd.find(V)) {
      if (!Seen.insert(V).second)
        return false;            // cycle: cannot happen for valid input
      V = It->second;
    }
    Final[KV.first] = V;
  }
  auto Subst = [&](Value *V) {
    auto It = Final.find(V);
    return It == Final.end() ? V : It->second;
  };

  // Every in-window instruction that survives must be placed exactly once.
  std::vector<Instruction *> Seq;
  std::set<Instruction *> Placed;
  std::function<void(Instruction *)> Place = [&](Instruction *I) {
    if (!InWin.count(I) || Dropped.count(I) || !Placed.insert(I).second)
      return;
    for (Value *Op : I->operands())
      if (auto *OI = dyn_cast<Instruction>(Subst(Op)))
        Place(OI);
    Seq.push_back(I);
  };
  for (unsigned Idx : P.Order)
    Place(C.MemOps[Idx]);
  for (Instruction *I : C.Window)
    Place(I);

  std::map<Instruction *, unsigned> Pos;
  for (unsigned I = 0; I < Seq.size(); ++I)
    Pos[Seq[I]] = I;
  for (Instruction *I : Seq)
    for (Value *Op : I->operands())
      if (auto *OI = dyn_cast<Instruction>(Subst(Op)))
        if (InWin.count(OI) && !Dropped.count(OI) && Pos[OI] >= Pos[I])
          return false;

  {
    std::vector<Instruction *> Want;
    for (unsigned Idx : P.Order)
      Want.push_back(C.MemOps[Idx]);
    std::vector<Instruction *> Got;
    for (Instruction *I : Seq)
      if (isa<LoadInst>(I) || isa<StoreInst>(I))
        Got.push_back(I);
    if (Want != Got)
      return false;
  }

  for (auto &KV : Final)
    KV.first->replaceAllUsesWith(KV.second);
  for (Instruction *D : Dropped)
    D->eraseFromParent();
  for (Instruction *I : Seq)
    I->moveBefore(C.Anchor->getIterator());
  return true;
}
