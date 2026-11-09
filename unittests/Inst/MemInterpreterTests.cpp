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

#include "souper/Infer/MemInterpreter.h"
#include "souper/Inst/Inst.h"
#include "souper/Inst/MemSchedule.h"
#include "gtest/gtest.h"

using namespace souper;

namespace {

const uint64_t Base = 0x10000;

MemAttrs attrs(uint32_t Align) {
  MemAttrs A;
  A.Align = Align;
  return A;
}

// Runs "store <val> -> p+Off" operations in order, then loads, on one pointer.
struct Rig {
  InstContext IC;
  Inst *P;
  Inst *Mem;
  std::vector<Inst *> Loads;
  Rig() {
    P = IC.createPtrVar(64, "p", PtrFacts());
    Mem = IC.getMemState();
  }
  Inst *at(uint64_t Off) {
    return Off ? IC.getPtrAdd(P, IC.getConst(llvm::APInt(64, Off)), false) : P;
  }
  void store(uint64_t Off, unsigned Bits, uint64_t V) {
    Mem = IC.getStore(Mem, at(Off), IC.getConst(llvm::APInt(Bits, V)),
                      attrs(1));
  }
  Inst *load(uint64_t Off, unsigned Bits) {
    Inst *L = IC.getLoad(Mem, at(Off), Bits, attrs(1));
    Loads.push_back(L);
    return L;
  }
  bool run(MemEndian E, MemRunResult &R) {
    MemSchedule S = linearize(IC.getMemResult(Mem, Loads));
    EXPECT_TRUE(S.Valid);
    MemTrial T;
    T.Seed = 7;
    T.VarVals[P] = llvm::APInt(64, Base);
    return runMemSchedule(S, T, R, E);
  }
};

} // namespace

TEST(MemEndianTest, ByteShift) {
  EXPECT_EQ(0u, memByteShift(0, 4, MemEndian::Little));
  EXPECT_EQ(24u, memByteShift(3, 4, MemEndian::Little));
  EXPECT_EQ(24u, memByteShift(0, 4, MemEndian::Big));
  EXPECT_EQ(0u, memByteShift(3, 4, MemEndian::Big));
  EXPECT_EQ(0u, memByteShift(0, 1, MemEndian::Big));
}

TEST(MemEndianTest, StoreI32ByteLayout) {
  for (MemEndian E : {MemEndian::Little, MemEndian::Big}) {
    Rig R;
    R.store(0, 32, 0x01020304);
    R.load(0, 32);
    MemRunResult Res;
    ASSERT_TRUE(R.run(E, Res));
    const uint8_t Little[] = {0x04, 0x03, 0x02, 0x01};
    const uint8_t Big[] = {0x01, 0x02, 0x03, 0x04};
    for (unsigned B = 0; B < 4; ++B)
      EXPECT_EQ(E == MemEndian::Little ? Little[B] : Big[B],
                Res.read(Base + B));
  }
}

TEST(MemEndianTest, RoundTripAllWidths) {
  for (MemEndian E : {MemEndian::Little, MemEndian::Big}) {
    for (unsigned Bits : {8u, 16u, 32u, 64u}) {
      Rig R;
      uint64_t V = 0x0123456789ABCDEFULL;
      if (Bits < 64)
        V &= (1ULL << Bits) - 1;
      R.store(0, Bits, V);
      R.load(0, Bits);
      MemRunResult Res;
      ASSERT_TRUE(R.run(E, Res));
      ASSERT_EQ(1u, Res.LiveOuts.size());
      EXPECT_EQ(V, Res.LiveOuts[0].getZExtValue()) << "bits " << Bits;
    }
  }
}

TEST(MemEndianTest, NarrowLoadsOfAWideStore) {
  Rig LE;
  LE.store(0, 32, 0x01020304);
  LE.load(0, 8);
  LE.load(0, 16);
  LE.load(3, 8);
  MemRunResult A;
  ASSERT_TRUE(LE.run(MemEndian::Little, A));
  EXPECT_EQ(0x04u, A.LiveOuts[0].getZExtValue());
  EXPECT_EQ(0x0304u, A.LiveOuts[1].getZExtValue());
  EXPECT_EQ(0x01u, A.LiveOuts[2].getZExtValue());

  Rig BE;
  BE.store(0, 32, 0x01020304);
  BE.load(0, 8);
  BE.load(0, 16);
  BE.load(3, 8);
  MemRunResult B;
  ASSERT_TRUE(BE.run(MemEndian::Big, B));
  EXPECT_EQ(0x01u, B.LiveOuts[0].getZExtValue());
  EXPECT_EQ(0x0102u, B.LiveOuts[1].getZExtValue());
  EXPECT_EQ(0x04u, B.LiveOuts[2].getZExtValue());
}

TEST(MemEndianTest, OverlappingStores) {
  // i32 store, then an i8 store into its second byte, then an i32 load.
  Rig LE;
  LE.store(0, 32, 0x01020304);
  LE.store(1, 8, 0xEE);
  LE.load(0, 32);
  MemRunResult A;
  ASSERT_TRUE(LE.run(MemEndian::Little, A));
  EXPECT_EQ(0x0102EE04u, A.LiveOuts[0].getZExtValue());

  Rig BE;
  BE.store(0, 32, 0x01020304);
  BE.store(1, 8, 0xEE);
  BE.load(0, 32);
  MemRunResult B;
  ASSERT_TRUE(BE.run(MemEndian::Big, B));
  EXPECT_EQ(0x01EE0304u, B.LiveOuts[0].getZExtValue());
}

TEST(MemEndianTest, TrialsDistinguishEndiannessSensitiveRewrites) {
  // Reading byte 0 of a stored i32 is NOT the same as reading the low byte of
  // the value on a big-endian target. A rewrite that assumed little endian
  // (load i8 == trunc(v)) must be rejected for Big and accepted for Little.
  InstContext IC;
  Inst *P = IC.createPtrVar(64, "p", PtrFacts());
  Inst *V = IC.createVar(32, "v");
  Inst *M0 = IC.getMemState();
  Inst *S = IC.getStore(M0, P, V, attrs(1));
  Inst *L = IC.getLoad(S, P, 8, attrs(1));
  Inst *Lhs = IC.getMemResult(S, {L});
  Inst *Rhs = IC.getMemResult(S, {IC.getInst(Inst::Trunc, 8, {V})});
  EXPECT_TRUE(memTrialsAgree(Lhs, Rhs, 16, MemEndian::Little));
  EXPECT_FALSE(memTrialsAgree(Lhs, Rhs, 16, MemEndian::Big));
}
