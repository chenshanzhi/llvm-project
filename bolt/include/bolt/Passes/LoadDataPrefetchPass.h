//===- bolt/Passes/LoadDataPrefetchPass.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LoadDataPrefetchPass class, which insert prefetch
// instructions for delinquent load instructions.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_PASSES_LOADDATAPREFETCHPASS_H
#define BOLT_PASSES_LOADDATAPREFETCHPASS_H

#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Passes/BinaryPasses.h"

namespace llvm {
namespace bolt {

class LoadDataPrefetchPass : public BinaryFunctionPass {
  void runOnFunction(BinaryFunction &BF);

public:
  LoadDataPrefetchPass() : BinaryFunctionPass(false) {}

  const char *getName() const override { return "load-data-prefetch"; }

  Error runOnFunctions(BinaryContext &BC) override;
};

} // namespace bolt
} // namespace llvm

#endif

