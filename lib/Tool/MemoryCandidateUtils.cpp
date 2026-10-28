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

#include "souper/Tool/MemoryCandidateUtils.h"
#include "souper/Codegen/Codegen.h"
#include "souper/Extractor/Candidates.h"
#include "souper/Infer/MemorySynthesis.h"
#include "souper/Inst/MemCost.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"

using namespace llvm;
using namespace souper;

namespace {

std::string functionName(const Function &F) {
  if (F.hasLocalLinkage())
    return (F.getParent()->getModuleIdentifier() + ":" + F.getName()).str();
  return F.getName().str();
}

std::string oneLine(const Instruction *I) {
  std::string S;
  raw_string_ostream SS(S);
  I->print(SS);
  SS.flush();
  size_t B = S.find_first_not_of(" \t");
  return B == std::string::npos ? std::string() : S.substr(B);
}

void printPlan(raw_ostream &OS, const MemoryRegionCandidate &C,
               const MemPlan &P, int CostBefore, int CostAfter) {
  OS << "; Memory region: " << C.MemOps.size() << " accesses, cost "
     << CostBefore << " -> " << CostAfter << "\n";
  for (unsigned I = 0; I < C.MemOps.size(); ++I)
    OS << ";   [" << I << "] " << oneLine(C.MemOps[I]) << "\n";

  OS << "; New order:";
  for (unsigned Idx : P.Order)
    OS << " " << Idx;
  OS << "\n";
  for (auto &KV : P.Forward)
    OS << "; Load [" << KV.first << "] takes the value stored by [" << KV.second
       << "]\n";
  for (unsigned S : P.DeadStores)
    OS << "; Store [" << S << "] is removed (overwritten before any read)\n";
}

} // namespace

bool souper::SolveMemoryRegions(raw_ostream &OS, Module &M,
                                const MemoryOptFlags &Flags, bool Apply,
                                MemoryToolResult *Result) {
  MemoryToolResult Local;
  MemoryToolResult &R = Result ? *Result : Local;

  OS << "; Listing memory schedule replacements"
     << (Apply ? " (applied to the IR).\n" : ".\n");

  const unsigned MaxIterations = Apply ? 64 : 1;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    for (unsigned Iter = 0; Iter < MaxIterations; ++Iter) {
      PassBuilder PB;
      LoopAnalysisManager LAM;
      FunctionAnalysisManager FAM;
      CGSCCAnalysisManager CGAM;
      ModuleAnalysisManager MAM;
      PB.registerModuleAnalyses(MAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

      // Fresh per iteration: region nodes remember
      // the LLVM instructions they
      // came from, which may be erased
      // or reused once a plan is applied.
      InstContext IC;
      auto &LI = FAM.getResult<LoopAnalysis>(F);
      auto &DB = FAM.getResult<DemandedBitsAnalysis>(F);
      auto &LVI = FAM.getResult<LazyValueAnalysis>(F);
      auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
      auto &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
      auto &AA = FAM.getResult<AAManager>(F);

      MemoryRegionExtractor X(F, LI, DB, LVI, SE, TLI, AA, IC);
      MemSynthOptions O = Flags.Synth;
      Flags.applyTo(O.Cost);

      bool Applied = false;
      auto Regions = X.extract(Flags.MaxWindow);
      if (Iter == 0)
        R.Regions += Regions.size();

      for (auto &C : Regions) {
        std::vector<MemPlan> Plans;
        if (synthesizeMemoryReorders(X, C, IC, O, Plans) || Plans.empty())
          continue;
        Inst *RHS = X.build(C, Plans.front());
        if (!RHS)
          continue;
        int Before = memCost(C.Mapping.LHS, O.Cost);
        int After = memCost(RHS, O.Cost);
        if (Before - After <= 0)       // only strict improvements, as in the pass
          continue;

        OS << "\n; Function: " << functionName(F) << "\n";
        printPlan(OS, C, Plans.front(), Before, After);
        PrintReplacement(OS, C.BPCs, C.PCs, InstMapping(C.Mapping.LHS, RHS));
        ++R.Replacements;

        if (Apply) {
          if (!applyMemoryPlan(C, Plans.front()))
            continue;
          if (verifyFunction(F, &errs())) {
            errs() << "function " << F.getName()
                   << " broken after a memory rewrite\n";
            return false;
          }
          ++R.Applied;
          Applied = true;
          break;                         // re-analyse before looking further
        }
      }
      if (!Applied)
        break;
    }
  }
  return true;
}
