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

#ifndef SOUPER_INFER_MEMINTERPRETER_H
#define SOUPER_INFER_MEMINTERPRETER_H

#include "souper/Infer/Interpreter.h"
#include "souper/Inst/MemSchedule.h"
#include <cstdint>
#include <map>
#include <vector>

namespace souper {

/// One random trial: concrete values for every Var, plus a seed for the
/// contents of memory nobody has written yet.
struct MemTrial {
  std::map<const Inst *, llvm::APInt> VarVals;   // ints and pointer addresses
  uint64_t Seed = 0;
};

struct MemRunResult {
  std::vector<llvm::APInt> LiveOuts;
  std::map<uint64_t, uint8_t> Written;           // overlay on the seeded memory
  uint64_t Seed = 0;
  uint8_t read(uint64_t Addr) const;
};

/// Runs a linearised region. Returns false if some value was poison/UB or not
/// evaluable (the trial is then ignored).
bool runMemSchedule(const MemSchedule &S, const MemTrial &T, MemRunResult &R);

/// True if no trial tells LHS and RHS apart (live-outs and final memory).
/// Only ever used to REJECT a candidate; Alive2 stays the sole authority for
/// accepting one. Trials that violate a given path condition are skipped.
/// Conditions the PCs do not state (for example aliasing facts AA used) can still
/// make it reject a candidate that differs only on inputs those facts forbid. That costs an
/// optimisation, never soundness.
bool memTrialsAgree(Inst *LHS, Inst *RHS, unsigned NumTrials,
                    uint64_t Seed = 1,
                    const std::vector<InstMapping> *PCs = nullptr);

}

#endif  // SOUPER_INFER_MEMINTERPRETER_H
