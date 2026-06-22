//===- bolt/Passes/LoadDataPrefetchPass.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LoadDataPrefetch class.
//
//===----------------------------------------------------------------------===//

#include "bolt/Passes/LoadDataPrefetchPass.h"
#include "bolt/Core/BinaryFunctionCallGraph.h"
#include "bolt/Core/ParallelUtilities.h"
#include "bolt/Passes/DataflowInfoManager.h"
#include "bolt/Passes/LivenessAnalysis.h"
#include "bolt/Passes/RegAnalysis.h"

#define DEBUG_TYPE "load-data-prefetch"

namespace llvm {
namespace bolt {

void LoadDataPrefetchPass::runOnFunction(BinaryFunction &BF) {
  assert(BF.getLoadDataPrefetchCount() != 0);
  BinaryContext &BC = BF.getBinaryContext();

  LLVM_DEBUG(dbgs() << "Function: " << BF.getPrintName()
      << ", LoadDataPrefetchCount: " << BF.getLoadDataPrefetchCount() << '\n');

  std::unique_ptr<BinaryFunctionCallGraph> CG =
      std::make_unique<BinaryFunctionCallGraph>(buildCallGraph(BC));
  std::unique_ptr<RegAnalysis> RA =
      std::make_unique<RegAnalysis>(BC, &BC.getBinaryFunctions(), &*CG);
  DataflowInfoManager Info(BF, RA.get(), nullptr);
  LivenessAnalysis &LA = Info.getLivenessAnalysis();

  for (BinaryBasicBlock &BB : BF) {
    for (auto I = BB.begin(); I != BB.end(); ++I) {
      MCInst &Inst = *I;
      if (BC.MIB->hasAnnotation(Inst, "LoadDataPrefetch")) {
        auto L = BC.scopeLock();

        LLVM_DEBUG({
          dbgs() << "Found LoadDataPrefetch Inst: ";
          BC.printInstruction(dbgs(), Inst);
        });

        const int64_t PrfOffset =
            BC.MIB->getAnnotationAs<int64_t>(Inst, "LoadDataPrefetch");
        BC.MIB->removeAnnotation(Inst, "LoadDataPrefetch");

        ProgramPoint PP(&Inst);
        MCRegister UsableReg = LA.scavengeRegAfter(PP);

        LLVM_DEBUG(dbgs() << "UsableReg: " << UsableReg << '\n');

        // TODO: How to handle prefetch from different levels?
        auto Code = BC.MIB->createLoadDataPrefetch(Inst, PrfOffset, 0, UsableReg);

        LLVM_DEBUG({
          dbgs() << "--------------------------------------------------\n";
          dbgs() << Inst << '\n' << "-->\n";
          for (const auto &E : Code)
            dbgs() << E << '\n';
          dbgs() << "--------------------------------------------------\n";
        });

        I = BB.replaceInstruction(I, Code);
        std::advance(I, Code.size() - 1);
      }
    }
  }
}

Error LoadDataPrefetchPass::runOnFunctions(BinaryContext &BC) {
  ParallelUtilities::WorkFuncWithAllocTy WorkFunc =
      [&](BinaryFunction &BF, MCPlusBuilder::AllocatorIdTy AllocId) {
        runOnFunction(BF);
      };
  ParallelUtilities::PredicateTy SkipPred = [&](const BinaryFunction &BF) {
    return !BF.getLoadDataPrefetchCount();
  };
  ParallelUtilities::runOnEachFunctionWithUniqueAllocId(
      BC, ParallelUtilities::SchedulingPolicy::SP_INST_LINEAR, WorkFunc,
      SkipPred, "load-data-prefetch", /*ForceSequential=*/true);
  return Error::success();
}

} // namespace bolt
} // namespace llvm

