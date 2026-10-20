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

#ifndef SOUPER_INST_MEMSCHEDULE_H
#define SOUPER_INST_MEMSCHEDULE_H

#include "souper/Inst/Inst.h"
#include <vector>

namespace souper {

/// A memory region in execution order.
struct MemSchedule {
  Inst *Root = nullptr;           // the MemResult
  std::vector<Inst *> Ops;        // loads and stores, in execution order
  std::vector<Inst *> LiveOuts;   // Root->Ops[1..]
  bool Valid = false;
};

/// Walk the store chain from Root->Ops[0] back to MemState, then put every
/// reachable Load immediately after the store that produced its token.
/// Valid == false if the chain is malformed or a load hangs off a token that is
/// not on the chain.
MemSchedule linearize(Inst *MemResultRoot);

}

#endif  // SOUPER_INST_MEMSCHEDULE_H
