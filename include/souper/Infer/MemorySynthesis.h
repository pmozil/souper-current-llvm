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

#ifndef SOUPER_INFER_MEMORYSYNTHESIS_H
#define SOUPER_INFER_MEMORYSYNTHESIS_H

#include "souper/Extractor/Candidates.h"
#include "souper/Inst/Inst.h"
#include "souper/Inst/MemCost.h"
#include <system_error>
#include <vector>

namespace souper {

struct MemSynthOptions {
  unsigned MaxOrders = 2000;
  unsigned PruneTrials = 32;
  unsigned MaxVerify = 20;
  unsigned AliveAttempts = 3;
  MemCostContext Cost;
};

struct MemSynthStats {
  unsigned PlansBuilt = 0;
  unsigned Cheaper = 0;
  unsigned Pruned = 0;
  unsigned Verified = 0;
  unsigned Retries = 0;
};

/// Searches for a cheaper schedule of a memory region: reorderings of the
/// loads and stores, store-to-load forwarding and dead-store removal.
///
/// Verified is filled with plans that are strictly cheaper under memCost and
/// that Alive2 proved equivalent to the original, cheapest first. It holds at
/// most one plan (the cheapest valid one). Separate from the expression
/// synthesizers: it does not go through getGuesses or Solver::infer.
///
/// Returns invalid_argument if C.Mapping.LHS is not a linearisable region.
std::error_code synthesizeMemoryReorders(MemoryRegionExtractor &X,
                                         const MemoryRegionCandidate &C,
                                         InstContext &IC,
                                         const MemSynthOptions &O,
                                         std::vector<MemPlan> &Verified,
                                         MemSynthStats *Stats = nullptr);

}

#endif  // SOUPER_INFER_MEMORYSYNTHESIS_H
