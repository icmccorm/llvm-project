//===- Interpreter.cpp - Top-Level LLVM Interpreter Implementation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the top-level functionality for the LLVM interpreter.
// This interpreter is designed to be a very simple, portable, inefficient
// interpreter.
//
//===----------------------------------------------------------------------===//

#include "Interpreter.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include <cstring>
using namespace llvm;

namespace {

static struct RegisterInterp {
  RegisterInterp() { Interpreter::Register(); }
} InterpRegistrator;

} // namespace

extern "C" void LLVMLinkInInterpreter() {}

/// Create a new interpreter object.
///
ExecutionEngine *Interpreter::create(std::unique_ptr<Module> M,
                                     std::string *ErrStr) {
  // Tell this Module to materialize everything and release the GVMaterializer.
  if (Error Err = M->materializeAll()) {
    std::string Msg;
    handleAllErrors(std::move(Err),
                    [&](ErrorInfoBase &EIB) { Msg = EIB.message(); });
    if (ErrStr)
      *ErrStr = Msg;
    // We got an error, just return 0
    return nullptr;
  }

  return new Interpreter(std::move(M));
}

//===----------------------------------------------------------------------===//
// Interpreter ctor - Initialize stuff
//
Interpreter::Interpreter(std::unique_ptr<Module> M)
    : ExecutionEngine(std::move(M)) {
  // Initialize the "backend"
  initializeExecutionEngine();
  initializeExternalFunctions();

  IL = new IntrinsicLowering(getDataLayout());
}

Interpreter::~Interpreter() { delete IL; }

void Interpreter::runAtExitHandlers() {
  while (!AtExitHandlers.empty()) {
    callFunction(AtExitHandlers.back(), ArrayRef<GenericValue>());
    AtExitHandlers.pop_back();
    run();
  }
}

/// run - Start execution with the specified function and arguments.
///
GenericValue Interpreter::runFunction(Function *F,
                                      ArrayRef<GenericValue> ArgValues) {
  assert(F && "Function *F was null at entry to run()");
  Interpreter::pushPath();

  // Try extra hard not to pass extra args to a function that isn't
  // expecting them.  C programmers frequently bend the rules and
  // declare main() with fewer parameters than it actually gets
  // passed, and the interpreter barfs if you pass a function more
  // parameters than it is declared to take. This does not attempt to
  // take into account gratuitous differences in declared types,
  // though.
  const size_t ArgCount = F->getFunctionType()->getNumParams();
  ArrayRef<GenericValue> ActualArgs =
      ArgValues.slice(0, std::min(ArgValues.size(), ArgCount));

  // Set up the function call.
  callFunction(F, ActualArgs);

  // Start executing the function.
  run();

  return Interpreter::popPath();
}

void Interpreter::registerMiriErrorWithoutLocation() {
  ExecutionEngine::setMiriErrorFlag();
  ExecutionPath &CurrentPath = ExecutionPaths.back();
  for (ExecutionContext &CurrContext : CurrentPath.ECStack) {
    if (CurrContext.Caller) {
      DILocation *Loc = CurrContext.Caller->getDebugLoc();
      if (Loc) {
        StringRef ErrorFile = Loc->getFilename();
        StringRef ErrorDir = Loc->getDirectory();
        StackTrace.push_back(MiriErrorTrace{ErrorDir.data(), ErrorDir.size(),
                                            ErrorFile.data(), ErrorFile.size(),
                                            Loc->getLine(), Loc->getColumn()});
      }
    }
    if (Interpreter::miriIsInitialized()) {
      this->MiriStackTraceRecorder(this->MiriWrapper, StackTrace.data(),
                                   StackTrace.size());
    }
  }
}
void Interpreter::registerMiriError(Instruction &I) {
  DILocation *Loc = I.getDebugLoc();
  if (Loc) {
    StringRef ErrorFile = Loc->getFilename();
    StringRef ErrorDir = Loc->getDirectory();
    StackTrace.push_back(MiriErrorTrace{ErrorDir.data(), ErrorDir.size(),
                                        ErrorFile.data(), ErrorFile.size(),
                                        Loc->getLine(), Loc->getColumn()});
  }
  Interpreter::registerMiriErrorWithoutLocation();
}