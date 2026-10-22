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

#include "souper/Inst/Inst.h"
#include "souper/Inst/MemCost.h"
#include "souper/Inst/MemSchedule.h"
#include "gtest/gtest.h"

using namespace souper;

namespace {

MemAttrs attrs4() {
  MemAttrs A;
  A.Align = 4;
  return A;
}

// store %v -> p, then load q (the load is "late"), or the load first.
struct TwoAccessRegion {
  InstContext IC;
  Inst *P, *Q, *V, *Mem0, *Store, *LoadAfter, *LoadBefore;
  Inst *Late, *Early;
  TwoAccessRegion() {
    P = IC.createPtrVar(64, "p", PtrFacts());
    Q = IC.createPtrVar(64, "q", PtrFacts());
    V = IC.createVar(32, "v");
    Mem0 = IC.getMemState();
    Store = IC.getStore(Mem0, P, V, attrs4());
    LoadAfter = IC.getLoad(Store, Q, 32, attrs4());
    LoadBefore = IC.getLoad(Mem0, Q, 32, attrs4());
    Late = IC.getMemResult(Store, {LoadAfter});
    Early = IC.getMemResult(Store, {LoadBefore});
  }
};

} // namespace

TEST(MemInstTest, NodesAreUniqued) {
  InstContext IC;
  Inst *P = IC.createPtrVar(64, "p", PtrFacts());
  Inst *M0 = IC.getMemState();
  EXPECT_EQ(M0, IC.getMemState());
  Inst *L1 = IC.getLoad(M0, P, 32, attrs4());
  Inst *L2 = IC.getLoad(M0, P, 32, attrs4());
  EXPECT_EQ(L1, L2);

  // Access attributes are part of a node's identity.
  MemAttrs Volatile = attrs4();
  Volatile.Volatile = true;
  EXPECT_NE(L1, IC.getLoad(M0, P, 32, Volatile));
  MemAttrs Align8 = attrs4();
  Align8.Align = 8;
  EXPECT_NE(L1, IC.getLoad(M0, P, 32, Align8));
  // A different memory token is a different load.
  Inst *V = IC.createVar(32, "v");
  Inst *S = IC.getStore(M0, P, V, attrs4());
  EXPECT_NE(L1, IC.getLoad(S, P, 32, attrs4()));
}

TEST(MemInstTest, MemResultHasNominalWidthOne) {
  TwoAccessRegion R;
  EXPECT_EQ(1u, R.Late->Width);
  EXPECT_EQ(0u, R.Store->Width);
  EXPECT_EQ(0u, R.Mem0->Width);
}

TEST(MemScheduleTest, LoadsFollowTheStoreThatProducesTheirToken) {
  TwoAccessRegion R;
  MemSchedule Late = linearize(R.Late);
  ASSERT_TRUE(Late.Valid);
  ASSERT_EQ(2u, Late.Ops.size());
  EXPECT_EQ(Inst::Store, Late.Ops[0]->K);
  EXPECT_EQ(Inst::Load, Late.Ops[1]->K);

  MemSchedule Early = linearize(R.Early);
  ASSERT_TRUE(Early.Valid);
  ASSERT_EQ(2u, Early.Ops.size());
  EXPECT_EQ(Inst::Load, Early.Ops[0]->K);
  EXPECT_EQ(Inst::Store, Early.Ops[1]->K);
}

TEST(MemCostTest, HoistingALoadIsCheaper) {
  TwoAccessRegion R;
  MemCostContext Ctx;
  EXPECT_EQ(9, memCost(R.Late, Ctx));
  EXPECT_EQ(8, memCost(R.Early, Ctx));
  EXPECT_GT(memBenefit(R.Late, R.Early, Ctx), 0);
  EXPECT_LT(memBenefit(R.Early, R.Late, Ctx), 0);
  EXPECT_EQ(0, memBenefit(R.Late, R.Late, Ctx));   // strictness: no gain from a no-op
}

TEST(MemCostTest, CalibrationPutsForeignCostsOnTheBuiltInScale) {
  // A TTI-like model: every access costs 1 in the target's own units.
  {
    MemCostContext C;
    calibrateMemCost(C, 1);
    EXPECT_EQ(4, C.OpCostScale);
    EXPECT_EQ(1, C.UseStallPenalty);
    EXPECT_EQ(1, C.AdjacentBonus);
  }
  {
    MemCostContext C;
    calibrateMemCost(C, 2);
    EXPECT_EQ(2, C.OpCostScale);
  }
  {
    MemCostContext C;
    calibrateMemCost(C, 4);                       // already the built-in scale
    EXPECT_EQ(1, C.OpCostScale);
    EXPECT_EQ(1, C.UseStallPenalty);
  }
  {
    MemCostContext C;
    calibrateMemCost(C, 8);                       // expensive loads: other terms grow
    EXPECT_EQ(1, C.OpCostScale);
    EXPECT_EQ(2, C.UseStallPenalty);
    EXPECT_EQ(2, C.AdjacentBonus);
  }
  {
    MemCostContext C;                             // nonsense is ignored
    calibrateMemCost(C, 0);
    calibrateMemCost(C, -3);
    EXPECT_EQ(1, C.OpCostScale);
    EXPECT_EQ(1, C.UseStallPenalty);
  }
}

TEST(MemCostTest, CalibratedHookKeepsTheSameDecision) {
  TwoAccessRegion R;
  auto TtiLike = [](const Inst *I) -> std::optional<int> {
    if (I->K == Inst::Load || I->K == Inst::Store)
      return 1;
    return std::nullopt;
  };
  MemCostContext Raw;
  Raw.OpCostHook = TtiLike;
  MemCostContext Cal;
  Cal.OpCostHook = TtiLike;
  calibrateMemCost(Cal, 1);

  const Inst *Load = R.LoadAfter;
  EXPECT_EQ(1, memOpCost(Load, Raw));
  EXPECT_EQ(MemCostDefaults::LoadBase, memOpCost(Load, Cal));
  EXPECT_GT(memBenefit(R.Late, R.Early, Raw), 0);
  EXPECT_GT(memBenefit(R.Late, R.Early, Cal), 0);
}
