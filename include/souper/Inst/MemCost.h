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

#ifndef SOUPER_INST_MEMCOST_H
#define SOUPER_INST_MEMCOST_H

#include "souper/Inst/MemSchedule.h"
#include <functional>
#include <optional>

namespace souper {

namespace MemCostDefaults {
constexpr int LoadBase = 4;
constexpr int StoreBase = 2;
constexpr unsigned LoadLatency = 4;   // schedule slots a load needs before its use
constexpr int UseStallPenalty = 1;    // per missing slot
constexpr int AdjacentBonus = 1;      // adjacent, same-base, consecutive accesses
}

/// Everything the memory cost model may consult. Both hooks are optional, so
/// richer models (TTI, locality, register pressure) plug in without changing
/// any caller.
struct MemCostContext {
  /// Plug-in 1: override one access's cost (e.g. with TargetTransformInfo).
  /// Return nullopt to fall back to the built-in table.
  std::function<std::optional<int>(const Inst *)> OpCostHook;

  /// Plug-in 2: an extra whole-schedule term (locality, register pressure...).
  std::function<int(const MemSchedule &)> ExtraScheduleCost;

  unsigned LoadLatency = MemCostDefaults::LoadLatency;
};

/// Cost of one load or store (0 for anything else).
int memOpCost(const Inst *I, const MemCostContext &Ctx);

/// Cost of a linearised region: per-access costs plus schedule terms.
int memScheduleCost(const MemSchedule &S, const MemCostContext &Ctx);

/// Cost of a region root. A very large value if it cannot be linearised.
int memCost(Inst *MemResultRoot, const MemCostContext &Ctx);

/// cost(LHS) - cost(RHS): positive means RHS is cheaper.
int memBenefit(Inst *LHS, Inst *RHS, const MemCostContext &Ctx);

}

#endif  // SOUPER_INST_MEMCOST_H
