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

#ifndef SOUPER_INFER_MEMORYOPTIONS_H
#define SOUPER_INFER_MEMORYOPTIONS_H

#include "souper/Infer/MemorySynthesis.h"
#include <optional>

namespace souper {

struct MemoryOptFlags {
  bool Enabled = false;            // -souper-mem-opt
  unsigned MaxWindow = 12;         // -souper-mem-max-window
  MemSynthOptions Synth;           // orders, trials, verify budget, retries
  std::optional<unsigned> LoadLatency;   // -souper-mem-load-latency, only if given

  void applyTo(MemCostContext &C) const {
    if (LoadLatency)
      C.LoadLatency = *LoadLatency;
  }
};

MemoryOptFlags getMemoryOptFlags();

}

#endif  // SOUPER_INFER_MEMORYOPTIONS_H
