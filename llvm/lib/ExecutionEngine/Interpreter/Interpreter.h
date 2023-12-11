//===-- Interpreter.h ------------------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines the interpreter structure
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_INTERPRETER_INTERPRETER_H
#define LLVM_LIB_EXECUTIONENGINE_INTERPRETER_INTERPRETER_H

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_map>
namespace llvm {

class IntrinsicLowering;
template <typename T> class generic_gep_type_iterator;
class ConstantExpr;
typedef generic_gep_type_iterator<User::const_op_iterator> gep_type_iterator;

// AllocaHolder - Object to track all of the blocks of memory allocated by
// alloca.  When the function returns, this object is popped off the execution
// stack, which causes the dtor to be run, which frees all the alloca'd memory.
//
class AllocaHolder {
  std::vector<void *> Allocations;

public:
  AllocaHolder() = default;

  // Make this type move-only.
  AllocaHolder(AllocaHolder &&) = default;
  AllocaHolder &operator=(AllocaHolder &&RHS) = default;

  ~AllocaHolder() {
    for (void *Allocation : Allocations)
      free(Allocation);
  }

  void add(void *Mem) { Allocations.push_back(Mem); }
};

class MiriAllocaHolder {
  std::vector<MiriPointer> MiriAllocations;
  MiriFreeHook MiriFree;
  void *MiriWrapper;

public:
  MiriAllocaHolder(void *Wrapper, MiriFreeHook Free) {
    MiriWrapper = Wrapper;
    MiriFree = Free;
  }
  // Make this type move-only.
  MiriAllocaHolder(MiriAllocaHolder &&) = default;
  MiriAllocaHolder &operator=(MiriAllocaHolder &&RHS) = default;

  ~MiriAllocaHolder() {
    for (MiriPointer Tracked : MiriAllocations)
      MiriFree(MiriWrapper, Tracked);
  }

  void add(MiriPointer Tracked) { MiriAllocations.push_back(Tracked); }
};

typedef std::vector<GenericValue> ValuePlaneTy;

// ExecutionContext struct - This struct represents one stack frame currently
// executing.
//
struct ExecutionContext {
  // Make this type move-only.
  ExecutionContext(ExecutionContext &&) = default;
  ExecutionContext &operator=(ExecutionContext &&RHS) = default;
  Function *CurFunction; // The currently executing function
  BasicBlock *CurBB;     // The currently executing BB
  Instruction *PreviousInst;
  BasicBlock::iterator CurInst; // The next instruction to execute
  CallBase *Caller;             // Holds the call that called subframes.
  bool MustResolvePendingReturn;
  GenericValue AwaitingReturn; // If non-null, the return value of the call into
                               // Rust NULL if main func or debugger invoked fn
  std::map<Value *, GenericValue> Values; // LLVM values used in this invocation
  std::vector<GenericValue> VarArgs;      // Values passed through an ellipsis
  AllocaHolder Allocas;                   // Track memory allocated by alloca
  MiriAllocaHolder MiriAllocas;
  ExecutionContext(void *Wrapper, MiriFreeHook MiriFree)
      : CurFunction(nullptr), CurBB(nullptr), PreviousInst(nullptr),
        CurInst(nullptr), Caller(nullptr), MustResolvePendingReturn(false),
        AwaitingReturn(GenericValue(0)), MiriAllocas(Wrapper, MiriFree) {}
};

class ExecutionThread {

public:
  // The runtime stack of executing code.  The top of the stack is the current
  // function record.
  std::vector<ExecutionContext> ECStack;
  GenericValue ExitValue; // The return value of the called function
  Type *DelayedReturn;
  std::vector<GenericValue> InitArgs;
  ExecutionThread() : DelayedReturn(nullptr) {
    memset(&ExitValue.Untyped, 0, sizeof(ExitValue.Untyped));
  }
  uint64_t ThreadID;
  // Make this type move-only.
  ExecutionThread(ExecutionThread &&) = default;
  ExecutionThread &operator=(ExecutionThread &&RHS) = default;
};

// Interpreter - This class represents the entirety of the interpreter.
//
class Interpreter : public ExecutionEngine, public InstVisitor<Interpreter> {
  IntrinsicLowering *IL;

  // AtExitHandlers - List of functions to call when the program exits,
  // registered with the atexit() library function.
  std::vector<Function *> AtExitHandlers;
  std::vector<MiriErrorTrace> StackTrace;

  std::unordered_map<uint64_t, ExecutionThread> Threads;
  uint64_t CurrentThreadID;

public:
  explicit Interpreter(std::unique_ptr<Module> M);
  ~Interpreter() override;

  /// runAtExitHandlers - Run any functions registered by the program's calls to
  /// atexit(3), which we intercept and store in AtExitHandlers.
  ///
  void runAtExitHandlers();

  static void Register() { InterpCtor = create; }

  /// Create an interpreter ExecutionEngine.
  ///
  static ExecutionEngine *create(std::unique_ptr<Module> M,
                                 std::string *ErrorStr = nullptr);

  /// run - Start execution with the specified function and arguments.
  ///
  GenericValue runFunction(Function *F,
                           ArrayRef<GenericValue> ArgValues) override;

  void *getPointerToNamedFunction(StringRef Name,
                                  bool AbortOnFailure = true) override {
    // FIXME: not implemented.
    return nullptr;
  }

  ExecutionContext *callingContext() {
    if (getCurrentThread()->ECStack.size() < 2) {
      return nullptr;
    } else {
      return &getCurrentThread()
                  ->ECStack[getCurrentThread()->ECStack.size() - 2];
    }
  }

  ExecutionContext &context() {
    if (getCurrentThread()->ECStack.empty()) {
      llvm_unreachable("Empty stack");
    } else {
      return getCurrentThread()->ECStack.back();
    }
  }

  GenericValue *getThreadExitValue() { return &getCurrentThread()->ExitValue; }

  void setExitValue(GenericValue Val) { getCurrentThread()->ExitValue = Val; }

  ArrayRef<GenericValue> createThreadContext(uint64_t ThreadID,
                                             GenericValue **Args,
                                             uint64_t NumArgs) {
    Threads[ThreadID] = ExecutionThread();
    Threads[ThreadID].InitArgs.resize(NumArgs);
    for (uint64_t i = 0; i < NumArgs; i++) {
      Threads[ThreadID].InitArgs[i] = *Args[i];
    }
    return Threads[ThreadID].InitArgs;
  }

  uint64_t switchThread(uint64_t ThreadID) {
    uint64_t OldThreadID = CurrentThreadID;
    CurrentThreadID = ThreadID;
    return OldThreadID;
  }

  ExecutionThread *getCurrentThread() {
    ExecutionThread *CurrThread = getThread(CurrentThreadID);
    if (CurrThread == nullptr) {
      report_fatal_error("Current thread not found");
    } else {
      return CurrThread;
    }
  }

  GenericValue getPendingReturnValue() {
    if (getCurrentThread()->ECStack.size() == 0) {
      report_fatal_error("Cannot resolve pending return value; stack is empty.");
    }
    return getCurrentThread()->ECStack.back().AwaitingReturn;
  }

  ExecutionThread *getThread(uint64_t ThreadID) {
    auto it = Threads.find(ThreadID);
    if (it == Threads.end())
      return nullptr;
    return &it->second;
  }

  void registerMiriErrorWithoutLocation();

  void registerMiriError(Instruction &I);

  void popContext() { Interpreter::getCurrentThread()->ECStack.pop_back(); }

  std::vector<ExecutionContext> &currentStack() {
    return getCurrentThread()->ECStack;
  }

  bool atStackBottom() { return getCurrentThread()->ECStack.size() == 1; }

  bool stackIsEmpty() { return getCurrentThread()->ECStack.empty(); }

  size_t stackSize() { return getCurrentThread()->ECStack.size(); }

  void clearStack() { getCurrentThread()->ECStack.clear(); }

  // Methods used to execute code:
  // Place a call on the stack
  void callFunction(Function *F, ArrayRef<GenericValue> ArgVals);
  void run(); // Execute instructions until nothing left to do
  void createThread(uint64_t NextThreadID, Function *F,
                    GenericValue** Args, uint64_t NumArgs) override;
  bool stepThread(uint64_t ThreadID, GenericValue *PendingReturnValue)
      override; // Execute a single instruction
  GenericValue *getThreadExitValueByID(uint64_t ThreadID) override;
  bool hasThread(uint64_t ThreadID) override;
  void terminateThread(uint64_t ThreadID) override;

  // Opcode Implementations
  void visitReturnInst(ReturnInst &I);
  void visitBranchInst(BranchInst &I);
  void visitSwitchInst(SwitchInst &I);
  void visitIndirectBrInst(IndirectBrInst &I);

  void visitUnaryOperator(UnaryOperator &I);
  void visitBinaryOperator(BinaryOperator &I);
  void visitICmpInst(ICmpInst &I);
  void visitFCmpInst(FCmpInst &I);
  void visitAllocaInst(AllocaInst &I);
  void visitLoadInst(LoadInst &I);
  void visitStoreInst(StoreInst &I);
  void visitGetElementPtrInst(GetElementPtrInst &I);
  void visitPHINode(PHINode &PN) {
    llvm_unreachable("PHI nodes already handled!");
  }
  void visitTruncInst(TruncInst &I);
  void visitZExtInst(ZExtInst &I);
  void visitSExtInst(SExtInst &I);
  void visitFPTruncInst(FPTruncInst &I);
  void visitFPExtInst(FPExtInst &I);
  void visitUIToFPInst(UIToFPInst &I);
  void visitSIToFPInst(SIToFPInst &I);
  void visitFPToUIInst(FPToUIInst &I);
  void visitFPToSIInst(FPToSIInst &I);
  void visitPtrToIntInst(PtrToIntInst &I);
  void visitIntToPtrInst(IntToPtrInst &I);
  void visitBitCastInst(BitCastInst &I);
  void visitSelectInst(SelectInst &I);

  void visitVAStartInst(VAStartInst &I);
  void visitVAEndInst(VAEndInst &I);
  void visitVACopyInst(VACopyInst &I);
  void visitIntrinsicInst(IntrinsicInst &I);
  void visitCallBase(CallBase &I);
  void visitUnreachableInst(UnreachableInst &I);

  void visitShl(BinaryOperator &I);
  void visitLShr(BinaryOperator &I);
  void visitAShr(BinaryOperator &I);

  void visitVAArgInst(VAArgInst &I);
  void visitExtractElementInst(ExtractElementInst &I);
  void visitInsertElementInst(InsertElementInst &I);
  void visitShuffleVectorInst(ShuffleVectorInst &I);

  void visitExtractValueInst(ExtractValueInst &I);
  void visitInsertValueInst(InsertValueInst &I);

  void visitInstruction(Instruction &I) {
    std::string Message =
        "LLVM instruction not supported: " + std::string(I.getOpcodeName());
    report_fatal_error(Message.data());
  }

  GenericValue *resolveReturnPlaceLocation(ExecutionContext *CallingContext,
                                           Type *RetTy);

  void callExternalFunction(Function *F, ArrayRef<GenericValue> ArgVals);

  void CallMiriFunctionByName(Function *F, ArrayRef<GenericValue> ArgVals);
  void CallMiriFunctionByPointer(FunctionType *FType, GenericValue FuncPtr,
                                 ArrayRef<GenericValue> ArgVals);

  void exitCalled(GenericValue GV);

  void addAtExitHandler(Function *F) { AtExitHandlers.push_back(F); }

  GenericValue *getFirstVarArg() { return &(this->context().VarArgs[0]); }

private: // Helper functions
  GenericValue executeGEPOperation(Value *Ptr, gep_type_iterator I,
                                   gep_type_iterator E, ExecutionContext &SF);

  // SwitchToNewBasicBlock - Start execution in a new basic block and run any
  // PHI nodes in the top of the block.  This is used for intraprocedural
  // control flow.
  //
  void SwitchToNewBasicBlock(BasicBlock *Dest, ExecutionContext &SF);

  void *getPointerToFunction(Function *F) override { return (void *)F; }

  void initializeExecutionEngine() {}
  // void initializeExternalFunctions();
  GenericValue getConstantExprValue(ConstantExpr *CE, ExecutionContext &SF);
  GenericValue getOperandValue(Value *V, ExecutionContext &SF);
  GenericValue executeTruncInst(Value *SrcVal, Type *DstTy,
                                ExecutionContext &SF);
  GenericValue executeSExtInst(Value *SrcVal, Type *DstTy,
                               ExecutionContext &SF);
  GenericValue executeZExtInst(Value *SrcVal, Type *DstTy,
                               ExecutionContext &SF);
  GenericValue executeFPTruncInst(Value *SrcVal, Type *DstTy,
                                  ExecutionContext &SF);
  GenericValue executeFPExtInst(Value *SrcVal, Type *DstTy,
                                ExecutionContext &SF);
  GenericValue executeFPToUIInst(Value *SrcVal, Type *DstTy,
                                 ExecutionContext &SF);
  GenericValue executeFPToSIInst(Value *SrcVal, Type *DstTy,
                                 ExecutionContext &SF);
  GenericValue executeUIToFPInst(Value *SrcVal, Type *DstTy,
                                 ExecutionContext &SF);
  GenericValue executeSIToFPInst(Value *SrcVal, Type *DstTy,
                                 ExecutionContext &SF);
  GenericValue executePtrToIntInst(Value *SrcVal, Type *DstTy,
                                   ExecutionContext &SF);
  GenericValue executeIntToPtrInst(Value *SrcVal, Type *DstTy,
                                   ExecutionContext &SF);
  GenericValue executeBitCastInst(Value *SrcVal, Type *DstTy,
                                  ExecutionContext &SF);
  void popStackAndReturnValueToCaller(Type *RetTy, GenericValue Result);

  void enterLowerStackFrame(Type *RetTy);

  void passReturnValueToLowerStackFrame(Type *RetTy, GenericValue Result);
};

} // namespace llvm

#endif
