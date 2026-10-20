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
  std::map<Value *, Value *> Fwd;               // forwarded load -> stored value
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
  auto Subst = [&](Value *V) {
    auto It = Fwd.find(V);
    return It == Fwd.end() ? V : It->second;
  };

  // Every in-window instruction that survives must be placed exactly once.
  std::vector<Instruction *> Seq;
  std::set<Instruction *> Placed;
  std::function<void(Instruction *)> Place = [&](Instruction *I) {
    if (!InWin.count(I) || Dropped.count(I) || !Placed.insert(I).second)
      return;
    for (Value *Op : I->operands())
      if (auto *OI = dyn_cast<Instruction>(Subst(Op)))
        Place(OI);               // operand definitions first, original order
    Seq.push_back(I);
  };
  for (unsigned Idx : P.Order)
    Place(C.MemOps[Idx]);
  for (Instruction *I : C.Window)  // stragglers: pure values used only after the window
    Place(I);

  // Dry run: every in-window operand must precede its user. Operands defined
  // outside the window dominate all of it, and the window keeps its extent,
  // so they need no check.
  std::map<Instruction *, unsigned> Pos;
  for (unsigned I = 0; I < Seq.size(); ++I)
    Pos[Seq[I]] = I;
  for (Instruction *I : Seq)
    for (Value *Op : I->operands())
      if (auto *OI = dyn_cast<Instruction>(Subst(Op)))
        if (InWin.count(OI) && !Dropped.count(OI) && Pos[OI] >= Pos[I])
          return false;

  // The memory ops must appear in exactly the planned order.
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

  // Commit. Forwarded loads: redirect their users, then erase.
  for (auto &KV : Fwd)
    KV.first->replaceAllUsesWith(KV.second);
  for (Instruction *D : Dropped)
    D->eraseFromParent();
  for (Instruction *I : Seq)
    I->moveBefore(C.Anchor->getIterator());   // keeps metadata and debug info
  return true;
}
