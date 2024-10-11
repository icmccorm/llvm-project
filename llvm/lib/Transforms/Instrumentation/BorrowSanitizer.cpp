//===- BorrowSanitizer.cpp - Instrumentation for overflow defense
//------------===//

#include "llvm/Transforms/Instrumentation/BorrowSanitizer.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "bsan"

BorrowSanitizerOptions::BorrowSanitizerOptions(){}

PreservedAnalyses BorrowSanitizerPass::run(Function &F,
                                           FunctionAnalysisManager &FAM) {
  return PreservedAnalyses::all();
}

PreservedAnalyses ModuleBorrowSanitizerPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  return PreservedAnalyses::all();
}