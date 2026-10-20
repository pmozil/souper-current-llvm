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

#include "souper/Inst/MemSchedule.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

using namespace souper;

MemSchedule souper::linearize(Inst *Root) {
  MemSchedule S;
  S.Root = Root;
  if (!Root || Root->K != Inst::MemResult || Root->Ops.empty())
    return S;

  // 1. Token chain, newest first, then reverse so Chain[0] is MemState.
  std::vector<Inst *> Chain;
  for (Inst *T = Root->Ops[0];; T = T->Ops[0]) {
    Chain.push_back(T);
    if (T->K == Inst::MemState)
      break;
    if (T->K != Inst::Store)
      return S;                  // only stores advance memory in v1
  }
  std::reverse(Chain.begin(), Chain.end());
  std::map<Inst *, unsigned> TokenIdx;
  for (unsigned I = 0; I < Chain.size(); ++I)
    TokenIdx[Chain[I]] = I;

  // 2. Bucket every load by the token it observes. DFS post-order means a load
  //    whose pointer depends on another load lands after it.
  std::vector<std::vector<Inst *>> LoadsAt(Chain.size());
  std::set<Inst *> Seen;
  bool Ok = true;
  std::function<void(Inst *)> Visit = [&](Inst *I) {
    if (!Seen.insert(I).second)
      return;
    if (I->K == Inst::MemState || I->K == Inst::Store)
      return;                    // handled through Chain
    if (I->K == Inst::Load) {
      auto It = TokenIdx.find(I->Ops[0]);
      if (It == TokenIdx.end()) {
        Ok = false;
        return;
      }
      Visit(I->Ops[1]);
      LoadsAt[It->second].push_back(I);
      return;
    }
    for (Inst *Op : I->Ops)
      Visit(Op);
  };
  for (unsigned I = 1; I < Root->Ops.size(); ++I)
    Visit(Root->Ops[I]);
  for (unsigned I = 1; I < Chain.size(); ++I) {
    Visit(Chain[I]->Ops[1]);     // store pointer
    Visit(Chain[I]->Ops[2]);     // store value
  }
  if (!Ok)
    return S;

  // 3. Token I is the memory state *after* store I; loads that observe it run
  //    after that store and before store I+1.
  for (unsigned I = 0; I < Chain.size(); ++I) {
    if (I > 0)
      S.Ops.push_back(Chain[I]);
    for (Inst *L : LoadsAt[I])
      S.Ops.push_back(L);
  }
  S.LiveOuts.assign(Root->Ops.begin() + 1, Root->Ops.end());
  S.Valid = true;
  return S;
}
