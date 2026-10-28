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

#ifndef SOUPER_TOOL_MEMORYCANDIDATEUTILS_H
#define SOUPER_TOOL_MEMORYCANDIDATEUTILS_H

#include "souper/Infer/MemoryOptions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

namespace souper {

struct MemoryToolResult {
  unsigned Regions = 0;        // regions found
  unsigned Replacements = 0;   // regions with a verified, cheaper schedule
  unsigned Applied = 0;        // plans applied to the IR (Apply mode only)
};

/// The memory-region counterpart of SolveCandidateMap.
bool SolveMemoryRegions(llvm::raw_ostream &OS, llvm::Module &M,
                        const MemoryOptFlags &Flags, bool Apply,
                        MemoryToolResult *Result = nullptr);

}

#endif  // SOUPER_TOOL_MEMORYCANDIDATEUTILS_H
