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

#include "souper/Infer/MemoryOptions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace souper;

static cl::opt<bool> MemOpt("souper-mem-opt", cl::init(false),
    cl::desc("Reorder, forward and delete memory operations (default=false)"));

static cl::opt<unsigned> MemMaxWindow("souper-mem-max-window", cl::init(12),
    cl::desc("Max loads+stores per memory region (default=12)"));

static cl::opt<unsigned> MemMaxOrders("souper-mem-max-orders", cl::init(2000),
    cl::desc("Max enumerated orders per move set (default=2000)"));

static cl::opt<unsigned> MemPruneTrials("souper-mem-prune-trials", cl::init(32),
    cl::desc("Concrete trials before asking Alive2 (default=32)"));

static cl::opt<unsigned> MemMaxVerify("souper-mem-max-verify", cl::init(20),
    cl::desc("Max plans sent to Alive2 per memory region (default=20)"));

static cl::opt<unsigned> MemAliveAttempts("souper-mem-alive-attempts",
    cl::init(3),
    cl::desc("Alive2 attempts per plan when the solver times out (default=3)"));

static cl::opt<unsigned> MemLoadLatency("souper-mem-load-latency",
    cl::init(MemCostDefaults::LoadLatency),
    cl::desc("Schedule slots a load needs before its use (default=4)"));

MemoryOptFlags souper::getMemoryOptFlags() {
  MemoryOptFlags F;
  F.Enabled = MemOpt;
  F.MaxWindow = MemMaxWindow;
  F.Synth.MaxOrders = MemMaxOrders;
  F.Synth.PruneTrials = MemPruneTrials;
  F.Synth.MaxVerify = MemMaxVerify;
  F.Synth.AliveAttempts = MemAliveAttempts;
  if (MemLoadLatency.getNumOccurrences())
    F.LoadLatency = MemLoadLatency;
  return F;
}
