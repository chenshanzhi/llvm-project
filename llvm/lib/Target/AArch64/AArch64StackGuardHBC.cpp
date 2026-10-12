//=- AArch64StackGuardHBC.cpp - Use FEAT_HBC for stack guard checks --------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FEAT_HBC (Armv8.8-A Hinted Conditional Branches) adds the BC.cond
// instruction, which behaves identically to B.cond but hints to the branch
// predictor that the branch is nearly always resolved the same way (even
// though it is not known at compile time which way that is). This makes it a
// natural fit for stack protector checks: the conditional branch to the
// failure block is almost never taken.
//
// This pass runs as the very last pre-emit step, i.e. after register
// allocation, machine block placement and branch relaxation. BC.cond is
// encoding-compatible with B.cond (same operand list, same 19-bit PC-relative
// immediate range), so the transformation is a pure opcode rewrite:
//
//   1. Locate the stack protector failure block(s): the block(s) containing
//      a call to the failure libcall (__stack_chk_fail, or
//      __stack_smash_handler on OpenBSD). Tail merging may have created
//      several copies.
//   2. Rewrite every Bcc whose target is one of those blocks to BCcc.
//
// Because the pass runs after every CFG-observing pass, no changes to
// analyzeBranch/insertBranch/... are required; the AsmPrinter emits BCcc
// through the exact same path as Bcc.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-stack-guard-hbc"

namespace {

class AArch64StackGuardHBCLegacy : public MachineFunctionPass {
public:
  static char ID;
  AArch64StackGuardHBCLegacy() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "AArch64 Stack Guard FEAT_HBC";
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

char AArch64StackGuardHBCLegacy::ID = 0;

} // namespace

INITIALIZE_PASS(AArch64StackGuardHBCLegacy, DEBUG_TYPE,
                "AArch64 Stack Guard FEAT_HBC", false, false)

/// Return true if \p MO refers to the stack protector failure libcall:
/// __stack_chk_fail (default) or __stack_smash_handler (OpenBSD).
static bool isStackProtectorFailTarget(const MachineOperand &MO) {
  StringRef Name;
  if (MO.isSymbol())
    Name = MO.getSymbolName();
  else if (MO.isGlobal())
    Name = MO.getGlobal()->getName();
  else
    return false;
  return Name == "__stack_chk_fail" || Name == "__stack_smash_handler";
}

static bool runAArch64StackGuardHBC(MachineFunction &MF) {
  const AArch64Subtarget &ST = MF.getSubtarget<AArch64Subtarget>();
  if (!ST.hasHBC())
    return false;

  // Quick bail-out if the function has no stack protector at all.
  if (!MF.getFrameInfo().hasStackProtectorIndex())
    return false;

  // Collect the failure block(s). All stack protector checks of a function
  // share a single failure block, but tail merging may have created more
  // than one copy of it.
  SmallPtrSet<MachineBasicBlock *, 4> FailMBBs;
  for (MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (MI.isCall())
        for (const MachineOperand &MO : MI.operands())
          if (isStackProtectorFailTarget(MO)) {
            FailMBBs.insert(&MBB);
            break;
          }

  if (FailMBBs.empty())
    return false;

  // Rewrite Bcc -> BCcc for every conditional branch targeting a failure
  // block. BC.cond is a drop-in replacement for B.cond: same operands, same
  // immediate range; it only adds the branch-predictor hint.
  const AArch64InstrInfo *TII = ST.getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB.terminators())
      if (MI.getOpcode() == AArch64::Bcc &&
          FailMBBs.contains(MI.getOperand(1).getMBB())) {
        MI.setDesc(TII->get(AArch64::BCcc));
        Changed = true;
      }

  return Changed;
}

bool AArch64StackGuardHBCLegacy::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  return runAArch64StackGuardHBC(MF);
}

PreservedAnalyses
AArch64StackGuardHBCPass::run(MachineFunction &MF,
                              MachineFunctionAnalysisManager &) {
  if (runAArch64StackGuardHBC(MF))
    return getMachineFunctionPassPreservedAnalyses();
  return PreservedAnalyses::all();
}

FunctionPass *llvm::createAArch64StackGuardHBCLegacyPass() {
  return new AArch64StackGuardHBCLegacy();
}
