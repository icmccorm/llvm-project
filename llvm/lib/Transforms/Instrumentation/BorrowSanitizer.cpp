//===- BorrowSanitizer.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/BorrowSanitizer.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "bsan"

PreservedAnalyses BorrowSanitizerPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  return PreservedAnalyses::all();
}