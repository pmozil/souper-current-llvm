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

// This file is #included at the end of Candidates.cpp, inside the same
// translation unit, because ExprBuilder lives in an anonymous namespace there.
// It implements MemoryRegionExtractor (declared in Candidates.h).
//
// What a "region" is: a run of instructions in one basic block that contains
// no barrier. A barrier is anything that is not a plain, integer, byte-sized
// load or store but touches memory or may not return (volatile/atomic
// accesses, fences, calls, memcpy, pointer- or float-typed accesses), plus PHIs
// and terminators.

namespace {

bool isRegionBarrier(const Instruction &I) {
  if (isa<PHINode>(I) || I.isTerminator())
    return true;
  if (!isGuaranteedToTransferExecutionToSuccessor(&I))
    return true;
  auto OkTy = [](Type *T) {
    return T->isIntegerTy() && T->getIntegerBitWidth() % 8 == 0 &&
           T->getIntegerBitWidth() <= 64;
  };
  if (auto *L = dyn_cast<LoadInst>(&I))
    return !L->isSimple() || !OkTy(L->getType());
  if (auto *S = dyn_cast<StoreInst>(&I))
    return !S->isSimple() || !OkTy(S->getValueOperand()->getType());
  return I.mayReadOrWriteMemory();
}

uint32_t clampAlign(Align A) {
  return (uint32_t)std::min<uint64_t>(A.value(), 1u << 30);
}

MemAttrs attrsOf(const LoadInst *L) {
  MemAttrs MA;
  MA.Align = clampAlign(L->getAlign());
  MA.NonTemporal = L->hasMetadata(LLVMContext::MD_nontemporal);
  return MA;
}

MemAttrs attrsOf(const StoreInst *S) {
  MemAttrs MA;
  MA.Align = clampAlign(S->getAlign());
  MA.NonTemporal = S->hasMetadata(LLVMContext::MD_nontemporal);
  return MA;
}

unsigned accessBits(const Instruction *I) {
  if (auto *L = dyn_cast<LoadInst>(I))
    return L->getType()->getIntegerBitWidth();
  return cast<StoreInst>(I)->getValueOperand()->getType()->getIntegerBitWidth();
}

// Does I (transitively, inside the window) read a value loaded in the window?
bool dependsOnWindowLoad(Instruction *I, const std::set<Instruction *> &Win,
                         std::set<Instruction *> &Seen) {
  if (!Win.count(I))
    return false;
  if (isa<LoadInst>(I))
    return true;
  if (!Seen.insert(I).second)
    return false;
  for (Value *Op : I->operands())
    if (auto *OI = dyn_cast<Instruction>(Op))
      if (dependsOnWindowLoad(OI, Win, Seen))
        return true;
  return false;
}

} // namespace

struct MemoryRegionExtractor::Impl {
  Function &F;
  Module *M;
  const LoopInfo &LI;
  DemandedBits &DB;
  LazyValueInfo &LVI;
  ScalarEvolution &SE;
  TargetLibraryInfo &TLI;
  AAResults &AA;
  InstContext &IC;
  const DataLayout &DL;
  ExprBuilderOptions Opts;
  // One context per candidate: it owns the Var Insts every plan for that
  // candidate must share.
  std::vector<std::unique_ptr<ExprBuilderContext>> BaseEBCs;

  Impl(Function &F, const LoopInfo &LI, DemandedBits &DB, LazyValueInfo &LVI,
       ScalarEvolution &SE, TargetLibraryInfo &TLI, AAResults &AA,
       InstContext &IC)
      : F(F), M(F.getParent()), LI(LI), DB(DB), LVI(LVI), SE(SE), TLI(TLI),
        AA(AA), IC(IC), DL(F.getParent()->getDataLayout()) {}

  Inst *buildRegion(const MemoryRegionCandidate &C, const MemPlan &P,
                    ExprBuilderContext &EBC, bool &Ok);
  bool isNoAlias(Instruction *A, Instruction *B);
  void computeDeps(MemoryRegionCandidate &C);
  void addAliasPCs(MemoryRegionCandidate &C, ExprBuilderContext &EBC);
  void strengthenPtrFacts(MemoryRegionCandidate &C, ExprBuilderContext &EBC);
  bool makeCandidate(BasicBlock &BB, const std::vector<Instruction *> &Window,
                     Instruction *Anchor,
                     std::vector<MemoryRegionCandidate> &Out);
};

// Builds the region's Souper DAG by walking the plan: loads and stores are
// created in plan order, threading the memory token, and every pure
// instruction is built on demand by ExprBuilder. Every window load is a
// live-out. That is sound because every pure value is a function of the inputs
// and the loads, so equal loads imply equal pure values.
Inst *MemoryRegionExtractor::Impl::buildRegion(const MemoryRegionCandidate &C,
                                               const MemPlan &P,
                                               ExprBuilderContext &EBC,
                                               bool &Ok) {
  Ok = false;
  ExprBuilder EB(Opts, M, LI, DB, LVI, SE, TLI, IC, EBC);
  EB.MemMode = true;
  std::set<Instruction *> Win(C.Window.begin(), C.Window.end());

  Inst *Mem = IC.getMemState();
  for (unsigned Idx : P.Order) {
    Instruction *Op = C.MemOps[Idx];
    if (auto *L = dyn_cast<LoadInst>(Op)) {
      Inst *Ptr = EB.get(L->getPointerOperand());
      if (!Ptr->IsPtr)
        return nullptr;
      Inst *Ld = IC.getLoad(Mem, Ptr, accessBits(L), attrsOf(L));
      if (!Ld->hasOrigin(L))
        Ld->Origins.push_back(L);
      EBC.InstMap[L] = Ld;
    } else {
      auto *S = cast<StoreInst>(Op);
      Inst *Ptr = EB.get(S->getPointerOperand());
      if (!Ptr->IsPtr)
        return nullptr;
      Inst *Val = EB.get(S->getValueOperand());
      Mem = IC.getStore(Mem, Ptr, Val, attrsOf(S));
      if (!Mem->hasOrigin(S))
        Mem->Origins.push_back(S);
      // Loads forwarded from this store take its value.
      for (auto &KV : P.Forward) {
        if (KV.second != Idx)
          continue;
        if (Val->Width != accessBits(C.MemOps[KV.first]))
          return nullptr;
        EBC.InstMap[C.MemOps[KV.first]] = Val;
      }
    }
  }

  std::vector<Inst *> LiveOuts;
  for (Instruction *Op : C.MemOps) {
    if (!isa<LoadInst>(Op))
      continue;
    auto It = EBC.InstMap.find(Op);
    if (It == EBC.InstMap.end() || !It->second)
      return nullptr;               // neither scheduled nor forwarded
    LiveOuts.push_back(It->second);
  }

  if (EB.Invalid)
    return nullptr;
  // An opaque value defined inside the window is only safe if it does not read
  // a loaded value; otherwise reordering could change it behind our back.
  for (Value *V : EB.Opaque) {
    auto *I = dyn_cast<Instruction>(V);
    if (!I)
      continue;
    std::set<Instruction *> Seen;
    if (dependsOnWindowLoad(I, Win, Seen))
      return nullptr;
  }

  Ok = true;
  return IC.getMemResult(Mem, LiveOuts);
}

// AA proves the two accesses disjoint. Alive2 cannot see TBAA or other
// metadata, so query without it.
bool MemoryRegionExtractor::Impl::isNoAlias(Instruction *A, Instruction *B) {
  MemoryLocation LA = MemoryLocation::get(A);
  MemoryLocation LB = MemoryLocation::get(B);
  LA.AATags = AAMDNodes();
  LB.AATags = AAMDNodes();
  return AA.alias(LA, LB) == AliasResult::NoAlias;
}

void MemoryRegionExtractor::Impl::computeDeps(MemoryRegionCandidate &C) {
  unsigned N = C.MemOps.size();
  C.Deps.assign(N, {});
  std::set<Instruction *> Win(C.Window.begin(), C.Window.end());
  std::map<Instruction *, unsigned> IdxOf;
  for (unsigned I = 0; I < N; ++I)
    IdxOf[C.MemOps[I]] = I;

  auto Add = [&](unsigned J, unsigned I) {
    auto &D = C.Deps[J];
    if (std::find(D.begin(), D.end(), I) == D.end())
      D.push_back(I);
  };

  // 1. Memory edges: at least one store, and AA cannot prove no-alias. Alive2
  //    cannot see TBAA or other metadata, so query without it. Orders justified
  //    only by metadata would be proposed and then rejected.
  for (unsigned J = 0; J < N; ++J) {
    for (unsigned I = 0; I < J; ++I) {
      if (isa<LoadInst>(C.MemOps[I]) && isa<LoadInst>(C.MemOps[J]))
        continue;
      if (!isNoAlias(C.MemOps[I], C.MemOps[J]))
        Add(J, I);
    }
  }

  // 2. Data edges: from every window load that feeds J's pointer or stored
  //    value (walking only through non-memory window instructions). This stops
  //    a store being hoisted above the load that produces its value.
  for (unsigned J = 0; J < N; ++J) {
    std::set<Instruction *> Seen;
    std::function<void(Value *)> Walk = [&](Value *V) {
      auto *I = dyn_cast<Instruction>(V);
      if (!I || !Win.count(I))
        return;
      auto It = IdxOf.find(I);
      if (It != IdxOf.end()) {
        Add(J, It->second);
        return;
      }
      if (!Seen.insert(I).second)
        return;
      for (Value *Op : I->operands())
        Walk(Op);
    };
    Instruction *Op = C.MemOps[J];
    if (auto *L = dyn_cast<LoadInst>(Op)) {
      Walk(L->getPointerOperand());
    } else {
      auto *S = cast<StoreInst>(Op);
      Walk(S->getPointerOperand());
      Walk(S->getValueOperand());
    }
  }
}

static Value *ptrOperandOf(Instruction *I);

// Every access in the window is UB if its pointer is invalid, and nothing in a
// window can allocate or free memory (those are barriers). So in every
// execution where the original code is UB-free, each pointer that is accessed
// directly was already valid for that many bytes, and aligned, when the window
// began. Saying so to Alive2 is sound (it only excludes executions where the
// source is UB anyway) and shrinks its search a great deal. Without it, queries
// that take a second can run past Alive2's 60 s limit.
void MemoryRegionExtractor::Impl::strengthenPtrFacts(MemoryRegionCandidate &C,
                                                     ExprBuilderContext &EBC) {
  ExprBuilder EB(Opts, M, LI, DB, LVI, SE, TLI, IC, EBC);
  EB.MemMode = true;
  for (Instruction *Op : C.MemOps) {
    Inst *P = EB.get(ptrOperandOf(Op));
    if (P->K != Inst::Var || !P->IsPtr)
      continue;
    uint64_t Bytes = accessBits(Op) / 8;
    P->PF.DerefBytes = std::max<uint64_t>(P->PF.DerefBytes, Bytes);
    uint32_t A = isa<LoadInst>(Op) ? clampAlign(cast<LoadInst>(Op)->getAlign())
                                   : clampAlign(cast<StoreInst>(Op)->getAlign());
    P->PF.Align = std::max<uint32_t>(P->PF.Align, A);
  }
}

static Value *ptrOperandOf(Instruction *I) {
  if (auto *L = dyn_cast<LoadInst>(I))
    return L->getPointerOperand();
  return cast<StoreInst>(I)->getPointerOperand();
}

static uint32_t alignOfAccess(Instruction *I) {
  if (auto *L = dyn_cast<LoadInst>(I))
    return clampAlign(L->getAlign());
  return clampAlign(cast<StoreInst>(I)->getAlign());
}

// Alive2 does not know what LLVM's alias analysis knows: two pointer arguments
// may be equal even when both are marked noalias, so a reorder AA proved legal
// would be rejected. Tell it, as a path condition, what AA proved: p != q for
// every pair of accesses AA says are disjoint.
//
// Only the cheap, exact form is emitted: both accesses have the same width and
// are aligned to it, so distinct addresses cannot overlap. A size-aware
// condition ("q - p >= size") is correct too but takes Alive2's solver more than
// a minute, so those pairs are skipped (the reorder then simply fails to verify).
// Pairs with the same base and constant offsets need no condition: Alive2 sees
// that for itself.
void MemoryRegionExtractor::Impl::addAliasPCs(MemoryRegionCandidate &C,
                                              ExprBuilderContext &EBC) {
  ExprBuilder EB(Opts, M, LI, DB, LVI, SE, TLI, IC, EBC);
  EB.MemMode = true;
  std::set<Inst *> Seen;
  auto Base = [](Inst *P) {
    while ((P->K == Inst::PtrAdd || P->K == Inst::PtrAddInBounds) &&
           P->Ops[1]->K == Inst::Const)
      P = P->Ops[0];
    return P;
  };
  for (unsigned J = 0; J < C.MemOps.size(); ++J) {
    for (unsigned I = 0; I < J; ++I) {
      Instruction *A = C.MemOps[I], *B = C.MemOps[J];
      if (isa<LoadInst>(A) && isa<LoadInst>(B))
        continue;
      if (!isNoAlias(A, B))
        continue;
      unsigned Bits = accessBits(A);
      if (Bits != accessBits(B) || Bits % 8 != 0 ||
          alignOfAccess(A) < Bits / 8 || alignOfAccess(B) < Bits / 8)
        continue;
      Inst *PA = EB.get(ptrOperandOf(A));
      Inst *PB = EB.get(ptrOperandOf(B));
      if (!PA->IsPtr || !PB->IsPtr || PA == PB || containsMemory(PA) ||
          containsMemory(PB) || Base(PA) == Base(PB))
        continue;
      Inst *Ne = IC.getInst(Inst::Ne, 1, {PA, PB});
      if (Seen.insert(Ne).second)
        C.PCs.push_back(InstMapping(Ne, IC.getConst(llvm::APInt(1, 1))));
    }
  }
}

bool MemoryRegionExtractor::Impl::makeCandidate(
    BasicBlock &BB, const std::vector<Instruction *> &Window,
    Instruction *Anchor, std::vector<MemoryRegionCandidate> &Out) {
  MemoryRegionCandidate C;
  C.BB = &BB;
  C.Window = Window;
  C.Anchor = Anchor;
  unsigned NumStores = 0;
  for (Instruction *I : Window) {
    if (isa<LoadInst>(I)) {
      C.MemOps.push_back(I);
    } else if (isa<StoreInst>(I)) {
      C.MemOps.push_back(I);
      ++NumStores;
    }
  }
  if (C.MemOps.size() < 2 || NumStores == 0)
    return false;

  computeDeps(C);

  MemPlan Identity;
  for (unsigned I = 0; I < C.MemOps.size(); ++I)
    Identity.Order.push_back(I);

  auto EBC = std::make_unique<ExprBuilderContext>();
  bool Ok = false;
  Inst *Root = buildRegion(C, Identity, *EBC, Ok);
  if (!Ok)
    return false;

  C.Mapping = InstMapping(Root, nullptr);
  strengthenPtrFacts(C, *EBC);
  addAliasPCs(C, *EBC);
  C.Id = BaseEBCs.size();
  BaseEBCs.push_back(std::move(EBC));
  Out.push_back(std::move(C));
  return true;
}

MemoryRegionExtractor::MemoryRegionExtractor(
    Function &F, const LoopInfo &LI, DemandedBits &DB, LazyValueInfo &LVI,
    ScalarEvolution &SE, TargetLibraryInfo &TLI, AAResults &AA, InstContext &IC)
    : PImpl(std::make_unique<Impl>(F, LI, DB, LVI, SE, TLI, AA, IC)) {}

MemoryRegionExtractor::~MemoryRegionExtractor() = default;

std::vector<MemoryRegionCandidate>
MemoryRegionExtractor::extract(unsigned MaxWindowOps) {
  std::vector<MemoryRegionCandidate> Out;
  // Alive2's pointer width must match what the extractor produced (64).
  if (PImpl->DL.getPointerSizeInBits(0) != 64 || MaxWindowOps < 2)
    return Out;

  for (BasicBlock &BB : PImpl->F) {
    std::vector<Instruction *> Cur;
    unsigned MemCount = 0;
    auto Flush = [&](Instruction *Anchor) {
      PImpl->makeCandidate(BB, Cur, Anchor, Out);
      Cur.clear();
      MemCount = 0;
    };
    for (Instruction &I : BB) {
      if (isa<PHINode>(I))
        continue;
      if (isRegionBarrier(I)) {
        Flush(&I);               // the barrier (or terminator) is the anchor
        continue;
      }
      bool IsMem = isa<LoadInst>(I) || isa<StoreInst>(I);
      if (IsMem && MemCount == MaxWindowOps)
        Flush(&I);               // window full; I starts the next one
      Cur.push_back(&I);
      if (IsMem)
        ++MemCount;
    }
    // Every block ends in a terminator, which flushed the last window.
  }
  return Out;
}

Inst *MemoryRegionExtractor::build(const MemoryRegionCandidate &C,
                                   const MemPlan &P) {
  ExprBuilderContext EBC;
  // Share inputs with the LHS: Vars and constants carry over, everything else
  // is rebuilt for this plan.
  for (auto &KV : PImpl->BaseEBCs[C.Id]->InstMap) {
    Inst *I = KV.second;
    if (I && (I->K == Inst::Var || I->K == Inst::Const))
      EBC.InstMap[KV.first] = I;
  }
  bool Ok = false;
  Inst *R = PImpl->buildRegion(C, P, EBC, Ok);
  return Ok ? R : nullptr;
}

// ExprBuilder::buildMemGEP: byte-offset form of a GEP, using the current
// GEPOperator API (replaces the dead, #if 0'd buildGEP above).
Inst *ExprBuilder::buildMemGEP(GetElementPtrInst *GEP) {
  unsigned PW = DL.getPointerSizeInBits(GEP->getPointerAddressSpace());
  APInt ConstOff(PW, 0);
  SmallMapVector<Value *, APInt, 4> VarOffs;
  Inst *Ptr = nullptr;
  if (PW == 64 && !isa<VectorType>(GEP->getType()) &&
      GEP->collectOffset(DL, PW, VarOffs, ConstOff))
    Ptr = get(GEP->getPointerOperand());
  if (!Ptr || !Ptr->IsPtr) {
    Invalid = true;
    return makeArrayRead(GEP);
  }
  bool IB = GEP->isInBounds();
  for (auto &KV : VarOffs) {
    Inst *Idx = get(KV.first);
    if (Idx->Width < PW)
      Idx = IC.getInst(Inst::SExt, PW, {Idx});
    else if (Idx->Width > PW)
      Idx = IC.getInst(Inst::Trunc, PW, {Idx});
    Inst *Scaled = KV.second.isOne()
                       ? Idx
                       : IC.getInst(Inst::Mul, PW, {Idx, IC.getConst(KV.second)});
    Ptr = IC.getPtrAdd(Ptr, Scaled, IB);
  }
  if (!ConstOff.isZero())
    Ptr = IC.getPtrAdd(Ptr, IC.getConst(ConstOff), IB);
  return Ptr;
}
