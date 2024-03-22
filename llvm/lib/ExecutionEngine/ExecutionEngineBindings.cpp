//===-- ExecutionEngineBindings.cpp - C bindings for EEs ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the C bindings for the ExecutionEngine library.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/CodeGenCWrappers.h"
#include "llvm/Target/TargetOptions.h"
#include <cstring>
#include <iostream>
#include <optional>
using namespace std;

using namespace llvm;

#define DEBUG_TYPE "jit"

// Wrapping the C bindings types.

static LLVMTargetMachineRef wrap(const TargetMachine *P) {
  return reinterpret_cast<LLVMTargetMachineRef>(const_cast<TargetMachine *>(P));
}

/*===-- Operations on generic values --------------------------------------===*/

LLVMGenericValueRef LLVMCreateGenericValueOfInt(LLVMTypeRef Ty,
                                                unsigned long long N,
                                                LLVMBool IsSigned) {
  GenericValue *GenVal = new GenericValue();
  GenVal->IntVal = APInt(unwrap<IntegerType>(Ty)->getBitWidth(), N, IsSigned);
  GenVal->ValueTy = unwrap(Ty);
  return wrap(GenVal);
}

LLVMGenericValueRef LLVMCreateGenericValueOfPointer(void *P) {
  GenericValue *GenVal = new GenericValue();
  GenVal->PointerVal = P;
  return wrap(GenVal);
}

LLVMGenericValueRef
LLVMCreateGenericValueOfMiriPointer(MiriPointer PointerMetaVal) {
  GenericValue *GenVal = new GenericValue();
  GenVal->PointerVal = (void *)(uintptr_t)PointerMetaVal.addr;
  GenVal->Provenance = PointerMetaVal.prov;
  return wrap(GenVal);
}

LLVMGenericValueRef
LLVMGetPointerToAggregateGenericValue(LLVMGenericValueRef GenValRef,
                                      uint64_t Index) {
  return wrap(&(*(unwrap(GenValRef)->AggregateVal.begin() + Index)));
}

size_t LLVMGetAggregateGenericValueLength(LLVMGenericValueRef GenValRef) {
  return unwrap(GenValRef)->AggregateVal.size();
}

MiriPointer LLVMGenericValueToMiriPointer(LLVMGenericValueRef GenValRef) {
  return GVTOMiriPointer(*unwrap(GenValRef));
}

LLVMGenericValueRef LLVMCreateAggregateGenericValue(uint64_t NumMembers) {
  GenericValue *GenVal = new GenericValue();
  return wrap(GenVal);
}

void LLVMGenericValueAppendAggregateValue(LLVMGenericValueRef GenVal,
                                          LLVMGenericValueRef GenValElement) {
  unwrap(GenVal)->AggregateVal.push_back(*unwrap(GenValElement));
}

void LLVMGenericValueEnsureCapacity(LLVMGenericValueRef GenVal,
                                    uint64_t Capacity) {
  unwrap(GenVal)->AggregateVal.resize(Capacity);
}

LLVMGenericValueRef LLVMCreateGenericValueOfFloat(LLVMTypeRef TyRef, double N) {
  GenericValue *GenVal = new GenericValue();
  switch (unwrap(TyRef)->getTypeID()) {
  case Type::FloatTyID:
    GenVal->FloatVal = N;
    break;
  case Type::DoubleTyID:
    GenVal->DoubleVal = N;
    break;
  default:
    llvm_unreachable("LLVMGenericValueToFloat supports only float and double.");
  }
  GenVal->ValueTy = unwrap(TyRef);
  return wrap(GenVal);
}

LLVMGenericValueRef LLVMCreateGenericValueOfFloatSingle(float N) {
  GenericValue *GenVal = new GenericValue();
  GenVal->FloatVal = N;
  return wrap(GenVal);
}

LLVMGenericValueRef LLVMCreateGenericValueOfFloatDouble(double N) {
  GenericValue *GenVal = new GenericValue();
  GenVal->DoubleVal = N;
  return wrap(GenVal);
}

LLVMGenericValueRef LLVMCreateGenericValueOfData(const uint8_t *Data,
                                                 uint32_t Len) {
  GenericValue *GenVal = new GenericValue();
  GenVal->IntVal = APInt(8 * Len, 0);
  LoadIntFromMemory(GenVal->IntVal, Data, Len);
  return wrap(GenVal);
}

void LLVMGenericValueSetDataValue(LLVMGenericValueRef GenVal, const uint8_t *Data,
                                 uint32_t Len) {
  GenericValue *GenValInner = unwrap(GenVal);
  GenValInner->IntVal = APInt(8 * Len, 0);
  LoadIntFromMemory(GenValInner->IntVal, Data, Len);
}

LLVMGenericValueRef
LLVMGenericValueArrayRefGetElementAt(LLVMGenericValueArrayRef GenArray,
                                     uint64_t Index) {
  return wrap(&(*unwrap(GenArray))[Index]);
}
uint64_t LLVMGenericValueArrayRefLength(LLVMGenericValueArrayRef GenArray) {
  return unwrap(GenArray)->size();
}

float LLVMGenericValueToFloatSingle(LLVMGenericValueRef GenVal) {
  return unwrap(GenVal)->FloatVal;
}

double LLVMGenericValueToFloatDouble(LLVMGenericValueRef GenVal) {
  return unwrap(GenVal)->DoubleVal;
}

unsigned LLVMGenericValueIntWidth(LLVMGenericValueRef GenValRef) {
  return unwrap(GenValRef)->IntVal.getBitWidth();
}

APIntPointer LLVMGenericValueToInt(LLVMGenericValueRef GenVal) {
  GenericValue *GenValInner = unwrap(GenVal);
  APIntPointer IntPointer;
  IntPointer.data = GenValInner->IntVal.getRawData();
  IntPointer.words = GenValInner->IntVal.getNumWords();
  return IntPointer;
}

void *LLVMGenericValueToPointer(LLVMGenericValueRef GenVal) {
  return unwrap(GenVal)->PointerVal;
}

double LLVMGenericValueToFloat(LLVMTypeRef TyRef, LLVMGenericValueRef GenVal) {
  switch (unwrap(TyRef)->getTypeID()) {
  case Type::FloatTyID:
    return unwrap(GenVal)->FloatVal;
  case Type::DoubleTyID:
    return unwrap(GenVal)->DoubleVal;
  default:
    llvm_unreachable("LLVMGenericValueToFloat supports only float and double.");
  }
}

LLVMTypeRef LLVMGenericValueGetTypeTag(LLVMGenericValueRef GenVal) {
  return wrap(unwrap(GenVal)->ValueTy);
}

void LLVMGenericValueSetTypeTag(LLVMGenericValueRef GenVal, LLVMTypeRef Type) {
  unwrap(GenVal)->ValueTy = unwrap(Type);
}

void LLVMGenericValueSetMiriPointerValue(LLVMGenericValueRef GenVal,
                                         MiriPointer PointerMetaVal) {
  unwrap(GenVal)->PointerVal = (void *)(uintptr_t)PointerMetaVal.addr;
  unwrap(GenVal)->Provenance = PointerMetaVal.prov;
}

void LLVMGenericValueSetDoubleValue(LLVMGenericValueRef GenVal,
                                    double DoubleVal) {
  unwrap(GenVal)->DoubleVal = DoubleVal;
}

void LLVMGenericValueSetFloatValue(LLVMGenericValueRef GenVal, float FloatVal) {

  unwrap(GenVal)->FloatVal = FloatVal;
}
void LLVMGenericValueSetIntValue(LLVMGenericValueRef GenVal, uint64_t *Data,
                                 uint64_t Bytes) {
  GenericValue *GenValInner = unwrap(GenVal);
  if (Bytes == 0) {
    GenValInner->IntVal = APInt();
  } else {
    uint64_t NumWords = (Bytes + 7) / 8;
    GenValInner->IntVal = APInt(8 * Bytes, ArrayRef<uint64_t>(Data, NumWords));
  }
}

void LLVMDisposeGenericValue(LLVMGenericValueRef GenVal) {
  delete unwrap(GenVal);
}

/*===-- Operations on execution engines -----------------------------------===*/

LLVMBool LLVMCreateExecutionEngineForModule(LLVMExecutionEngineRef *OutEE,
                                            LLVMModuleRef M, char **OutError) {
  std::string Error;
  EngineBuilder builder(std::unique_ptr<Module>(unwrap(M)));
  builder.setEngineKind(EngineKind::Either).setErrorStr(&Error);
  if (ExecutionEngine *EE = builder.create()) {
    *OutEE = wrap(EE);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

LLVMBool LLVMCreateInterpreterForModule(LLVMExecutionEngineRef *OutInterp,
                                        LLVMModuleRef M, char **OutError) {
  std::string Error;
  EngineBuilder builder(std::unique_ptr<Module>(unwrap(M)));
  builder.setEngineKind(EngineKind::Interpreter).setErrorStr(&Error);
  if (ExecutionEngine *Interp = builder.create()) {
    *OutInterp = wrap(Interp);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

LLVMBool LLVMCreateJITCompilerForModule(LLVMExecutionEngineRef *OutJIT,
                                        LLVMModuleRef M, unsigned OptLevel,
                                        char **OutError) {
  std::string Error;
  EngineBuilder builder(std::unique_ptr<Module>(unwrap(M)));
  builder.setEngineKind(EngineKind::JIT)
      .setErrorStr(&Error)
      .setOptLevel((CodeGenOpt::Level)OptLevel);
  if (ExecutionEngine *JIT = builder.create()) {
    *OutJIT = wrap(JIT);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

void LLVMInitializeMCJITCompilerOptions(LLVMMCJITCompilerOptions *PassedOptions,
                                        size_t SizeOfPassedOptions) {
  LLVMMCJITCompilerOptions options;
  memset(&options, 0, sizeof(options)); // Most fields are zero by default.
  options.CodeModel = LLVMCodeModelJITDefault;

  memcpy(PassedOptions, &options,
         std::min(sizeof(options), SizeOfPassedOptions));
}

LLVMBool
LLVMCreateMCJITCompilerForModule(LLVMExecutionEngineRef *OutJIT,
                                 LLVMModuleRef M,
                                 LLVMMCJITCompilerOptions *PassedOptions,
                                 size_t SizeOfPassedOptions, char **OutError) {
  LLVMMCJITCompilerOptions options;
  // If the user passed a larger sized options struct, then they were compiled
  // against a newer LLVM. Tell them that something is wrong.
  if (SizeOfPassedOptions > sizeof(options)) {
    *OutError = strdup(
        "Refusing to use options struct that is larger than my own; assuming "
        "LLVM library mismatch.");
    return 1;
  }

  // Defend against the user having an old version of the API by ensuring that
  // any fields they didn't see are cleared. We must defend against fields being
  // set to the bitwise equivalent of zero, and assume that this means "do the
  // default" as if that option hadn't been available.
  LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
  memcpy(&options, PassedOptions, SizeOfPassedOptions);

  TargetOptions targetOptions;
  targetOptions.EnableFastISel = options.EnableFastISel;
  std::unique_ptr<Module> Mod(unwrap(M));

  if (Mod)
    // Set function attribute "frame-pointer" based on
    // NoFramePointerElim.
    for (auto &F : *Mod) {
      auto Attrs = F.getAttributes();
      StringRef Value = options.NoFramePointerElim ? "all" : "none";
      Attrs = Attrs.addFnAttribute(F.getContext(), "frame-pointer", Value);
      F.setAttributes(Attrs);
    }

  std::string Error;
  EngineBuilder builder(std::move(Mod));
  builder.setEngineKind(EngineKind::JIT)
      .setErrorStr(&Error)
      .setOptLevel((CodeGenOpt::Level)options.OptLevel)
      .setTargetOptions(targetOptions);
  bool JIT;
  if (std::optional<CodeModel::Model> CM = unwrap(options.CodeModel, JIT))
    builder.setCodeModel(*CM);
  if (options.MCJMM)
    builder.setMCJITMemoryManager(
        std::unique_ptr<RTDyldMemoryManager>(unwrap(options.MCJMM)));
  if (ExecutionEngine *JIT = builder.create()) {
    *OutJIT = wrap(JIT);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

void LLVMDisposeExecutionEngine(LLVMExecutionEngineRef EE) {
  delete unwrap(EE);
}

void LLVMRunStaticConstructors(LLVMExecutionEngineRef EE) {
  unwrap(EE)->finalizeObject();
  unwrap(EE)->runStaticConstructorsDestructors(false);
}

void LLVMRunStaticDestructors(LLVMExecutionEngineRef EE) {
  unwrap(EE)->finalizeObject();
  unwrap(EE)->runStaticConstructorsDestructors(true);
}

int LLVMRunFunctionAsMain(LLVMExecutionEngineRef EE, LLVMValueRef F,
                          unsigned ArgC, const char *const *ArgV,
                          const char *const *EnvP) {
  unwrap(EE)->finalizeObject();

  std::vector<std::string> ArgVec(ArgV, ArgV + ArgC);
  return unwrap(EE)->runFunctionAsMain(unwrap<Function>(F), ArgVec, EnvP);
}

LLVMGenericValueRef LLVMRunFunction(LLVMExecutionEngineRef EE, LLVMValueRef F,
                                    unsigned NumArgs,
                                    LLVMGenericValueRef *Args) {
  unwrap(EE)->finalizeObject();

  std::vector<GenericValue> ArgVec;
  ArgVec.reserve(NumArgs);
  for (unsigned I = 0; I != NumArgs; ++I)
    ArgVec.push_back(*unwrap(Args[I]));

  GenericValue *Result = new GenericValue();
  *Result = unwrap(EE)->runFunction(unwrap<Function>(F), ArgVec);
  return wrap(Result);
}

void LLVMFreeMachineCodeForFunction(LLVMExecutionEngineRef EE, LLVMValueRef F) {
}

void LLVMAddModule(LLVMExecutionEngineRef EE, LLVMModuleRef M) {
  unwrap(EE)->addModule(std::unique_ptr<Module>(unwrap(M)));
}

LLVMBool LLVMRemoveModule(LLVMExecutionEngineRef EE, LLVMModuleRef M,
                          LLVMModuleRef *OutMod, char **OutError) {
  Module *Mod = unwrap(M);
  unwrap(EE)->removeModule(Mod);
  *OutMod = wrap(Mod);
  return 0;
}

LLVMBool LLVMFindFunction(LLVMExecutionEngineRef EE, const char *Name,
                          LLVMValueRef *OutFn) {
  if (Function *F = unwrap(EE)->FindFunctionNamed(Name)) {
    *OutFn = wrap(F);
    return 0;
  }
  return 1;
}

void *LLVMRecompileAndRelinkFunction(LLVMExecutionEngineRef EE,
                                     LLVMValueRef Fn) {
  return nullptr;
}

LLVMTargetDataRef LLVMGetExecutionEngineTargetData(LLVMExecutionEngineRef EE) {
  return wrap(&unwrap(EE)->getDataLayout());
}

LLVMTargetMachineRef
LLVMGetExecutionEngineTargetMachine(LLVMExecutionEngineRef EE) {
  return wrap(unwrap(EE)->getTargetMachine());
}

void LLVMAddGlobalMapping(LLVMExecutionEngineRef EE, LLVMValueRef Global,
                          void *Addr) {
  unwrap(EE)->addGlobalMapping(unwrap<GlobalValue>(Global), Addr);
}

void *LLVMGetPointerToGlobal(LLVMExecutionEngineRef EE, LLVMValueRef Global) {
  unwrap(EE)->finalizeObject();

  return unwrap(EE)->getPointerToGlobal(unwrap<GlobalValue>(Global));
}

void LLVMExecutionEngineSetMiriRegisterGlobalHook(
    LLVMExecutionEngineRef EE, MiriRegisterGlobalHook GlobalHook) {
  assert(GlobalHook && "GlobalHook must be non-null");
  unwrap(EE)->setMiriRegisterGlobalHook(GlobalHook);
}

void LLVMExecutionEngineInitializeConstructorDestructorLists(
    LLVMExecutionEngineRef EE) {
  unwrap(EE)->initializeConstructorDestructorLists();
}

uint64_t LLVMExecutionEngineGetConstructorCount(LLVMExecutionEngineRef EE) {
  return (uint64_t)unwrap(EE)->Constructors.size();
}

uint64_t LLVMExecutionEngineGetDestructorCount(LLVMExecutionEngineRef EE) {
  return (uint64_t)unwrap(EE)->Destructors.size();
}

LLVMValueRef LLVMExecutionEngineGetDestructorAtIndex(LLVMExecutionEngineRef EE,
                                                     uint64_t Index) {
  if (Index >= unwrap(EE)->Destructors.size()) {
    return NULL;
  }
  return wrap(unwrap(EE)->Destructors.at(Index));
}
LLVMValueRef LLVMExecutionEngineGetConstructorAtIndex(LLVMExecutionEngineRef EE,
                                                      uint64_t Index) {
  if (Index >= unwrap(EE)->Constructors.size()) {
    return NULL;
  }
  return wrap(unwrap(EE)->Constructors.at(Index));
}

uint64_t LLVMGetGlobalValueAddress(LLVMExecutionEngineRef EE,
                                   const char *Name) {
  return unwrap(EE)->getGlobalValueAddress(Name);
}

uint64_t LLVMGetFunctionAddress(LLVMExecutionEngineRef EE, const char *Name) {
  return unwrap(EE)->getFunctionAddress(Name);
}

LLVMBool LLVMExecutionEngineGetErrMsg(LLVMExecutionEngineRef EE,
                                      char **OutError) {
  assert(OutError && "OutError must be non-null");
  auto *ExecEngine = unwrap(EE);
  if (ExecEngine->hasError()) {
    *OutError = strdup(ExecEngine->getErrorMessage().c_str());
    ExecEngine->clearErrorMessage();
    return true;
  }
  return false;
}

void LLVMExecutionEngineSetMiriCallByNameHook(
    LLVMExecutionEngineRef EE, MiriCallByNameHook IncomingCallbackHook) {
  assert(IncomingCallbackHook && "IncomingCallbackHook must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriCallByName(IncomingCallbackHook);
}

void LLVMExecutionEngineSetMiriCallByPointerHook(
    LLVMExecutionEngineRef EE, MiriCallByPointerHook IncomingCallbackHook) {
  assert(IncomingCallbackHook && "IncomingCallbackHook must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriCallByPointer(IncomingCallbackHook);
}

void LLVMExecutionEngineSetMiriGetElementPointerHook(
    LLVMExecutionEngineRef EE,
    MiriGetElementPointerHook IncomingGetElementPointerHook) {
  assert(IncomingGetElementPointerHook &&
         "IncomingGetElementPointerHook must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriGetElementPointerHook(IncomingGetElementPointerHook);
}

void LLVMExecutionEngineSetMiriStackTraceRecorderHook(
    LLVMExecutionEngineRef EE,
    MiriStackTraceRecorderHook IncomingStackTraceRecorderHook) {
  assert(IncomingStackTraceRecorderHook &&
         "IncomingStackTraceRecorderHook must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriStackTraceRecorder(IncomingStackTraceRecorderHook);
}

void LLVMExecutionEngineSetMiriInterpCxWrapper(LLVMExecutionEngineRef EE,
                                               void *MiriWrapper) {
  assert(MiriWrapper && "MiriWrapper must be non-null");
  auto *ExecEngine = unwrap(EE);
  void *PrevWrapper = ExecEngine->MiriWrapper;
  ExecEngine->setMiriInterpCxWrapper(MiriWrapper);
  if (PrevWrapper == nullptr) {
    ExecEngine->emitGlobals();
  }
}
void LLVMExecutionEngineSetMiriLoadHook(LLVMExecutionEngineRef EE,
                                        MiriLoadStoreHook IncomingLoadHook) {
  assert(IncomingLoadHook && "IncomingLoadHook must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriLoadHook(IncomingLoadHook);
}

void LLVMExecutionEngineSetMiriStoreHook(LLVMExecutionEngineRef EE,
                                         MiriLoadStoreHook IncomingStoreHook) {
  assert(IncomingStoreHook && "IncomingStoreHook must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriStoreHook(IncomingStoreHook);
}

void LLVMExecutionEngineSetMiriMalloc(LLVMExecutionEngineRef EE,
                                      MiriAllocationHook IncomingMalloc) {
  assert(IncomingMalloc && "IncomingMalloc must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriMalloc(IncomingMalloc);
}

void LLVMExecutionEngineSetMiriFree(LLVMExecutionEngineRef EE,
                                    MiriFreeHook IncomingFree) {
  assert(IncomingFree && "IncomingFree must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriFree(IncomingFree);
}

void LLVMExecutionEngineSetMiriMemset(LLVMExecutionEngineRef EE,
                                      MiriMemset IncomingMemset) {
  assert(IncomingMemset && "IncomingMemset must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriMemset(IncomingMemset);
}

void LLVMExecutionEngineSetMiriMemcpy(LLVMExecutionEngineRef EE,
                                      MiriMemcpy IncomingMemcpy) {
  assert(IncomingMemcpy && "IncomingMemset must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriMemcpy(IncomingMemcpy);
}

void LLVMExecutionEngineSetMiriIntToPtr(LLVMExecutionEngineRef EE,
                                        MiriIntToPtr IncomingIntToPtr) {
  assert(IncomingIntToPtr && "IncomingIntToPtr must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriIntToPtr(IncomingIntToPtr);
}

void LLVMExecutionEngineSetMiriPtrToInt(LLVMExecutionEngineRef EE,
                                        MiriPtrToInt IncomingPtrToInt) {
  assert(IncomingPtrToInt && "IncomingPtrToInt must be non-null");
  auto *ExecEngine = unwrap(EE);
  ExecEngine->setMiriPtrToInt(IncomingPtrToInt);
}

LLVMBool LLVMExecutionEngineStepThread(LLVMExecutionEngineRef EE,
                                       uint64_t ThreadID,
                                       LLVMGenericValueRef PendingReturnVal) {
  auto *ExecEngine = unwrap(EE);
  return (LLVMBool)(ExecEngine->stepThread(ThreadID, unwrap(PendingReturnVal)));
}

LLVMGenericValueRef
LLVMExecutionEngineGetThreadExitValue(LLVMExecutionEngineRef EE,
                                      uint64_t ThreadID) {
  auto *ExecEngine = unwrap(EE);
  return wrap(ExecEngine->getThreadExitValueByID(ThreadID));
}

void LLVMExecutionEngineCreateThread(LLVMExecutionEngineRef EE,
                                     uint64_t ThreadID, LLVMValueRef F,
                                     unsigned NumArgs,
                                     LLVMGenericValueRef *Args) {
  auto *ExecEngine = unwrap(EE);
  ExecEngine->finalizeObject();
  ExecEngine->createThread(ThreadID, unwrap<Function>(F), unwrap(Args), NumArgs);
}

LLVMBool LLVMExecutionEngineHasThread(LLVMExecutionEngineRef EE,
                                      uint64_t ThreadID) {
  auto *ExecEngine = unwrap(EE);
  return ExecEngine->hasThread(ThreadID);
}

void LLVMExecutionEngineTerminateThread(LLVMExecutionEngineRef EE,
                                        uint64_t ThreadID) {
  auto *ExecEngine = unwrap(EE);
  ExecEngine->terminateThread(ThreadID);
}

/*===-- Operations on memory managers -------------------------------------===*/

namespace {

struct SimpleBindingMMFunctions {
  LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection;
  LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection;
  LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory;
  LLVMMemoryManagerDestroyCallback Destroy;
};

class SimpleBindingMemoryManager : public RTDyldMemoryManager {
public:
  SimpleBindingMemoryManager(const SimpleBindingMMFunctions &Functions,
                             void *Opaque);
  ~SimpleBindingMemoryManager() override;

  uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID,
                               StringRef SectionName) override;

  uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID, StringRef SectionName,
                               bool isReadOnly) override;

  bool finalizeMemory(std::string *ErrMsg) override;

private:
  SimpleBindingMMFunctions Functions;
  void *Opaque;
};

SimpleBindingMemoryManager::SimpleBindingMemoryManager(
    const SimpleBindingMMFunctions &Functions, void *Opaque)
    : Functions(Functions), Opaque(Opaque) {
  assert(Functions.AllocateCodeSection &&
         "No AllocateCodeSection function provided!");
  assert(Functions.AllocateDataSection &&
         "No AllocateDataSection function provided!");
  assert(Functions.FinalizeMemory && "No FinalizeMemory function provided!");
  assert(Functions.Destroy && "No Destroy function provided!");
}

SimpleBindingMemoryManager::~SimpleBindingMemoryManager() {
  Functions.Destroy(Opaque);
}

uint8_t *SimpleBindingMemoryManager::allocateCodeSection(
    uintptr_t Size, unsigned Alignment, unsigned SectionID,
    StringRef SectionName) {
  return Functions.AllocateCodeSection(Opaque, Size, Alignment, SectionID,
                                       SectionName.str().c_str());
}

uint8_t *SimpleBindingMemoryManager::allocateDataSection(uintptr_t Size,
                                                         unsigned Alignment,
                                                         unsigned SectionID,
                                                         StringRef SectionName,
                                                         bool isReadOnly) {
  return Functions.AllocateDataSection(Opaque, Size, Alignment, SectionID,
                                       SectionName.str().c_str(), isReadOnly);
}

bool SimpleBindingMemoryManager::finalizeMemory(std::string *ErrMsg) {
  char *errMsgCString = nullptr;
  bool result = Functions.FinalizeMemory(Opaque, &errMsgCString);
  assert((result || !errMsgCString) &&
         "Did not expect an error message if FinalizeMemory succeeded");
  if (errMsgCString) {
    if (ErrMsg)
      *ErrMsg = errMsgCString;
    free(errMsgCString);
  }
  return result;
}

} // anonymous namespace

LLVMMCJITMemoryManagerRef LLVMCreateSimpleMCJITMemoryManager(
    void *Opaque,
    LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection,
    LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection,
    LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory,
    LLVMMemoryManagerDestroyCallback Destroy) {

  if (!AllocateCodeSection || !AllocateDataSection || !FinalizeMemory ||
      !Destroy)
    return nullptr;

  SimpleBindingMMFunctions functions;
  functions.AllocateCodeSection = AllocateCodeSection;
  functions.AllocateDataSection = AllocateDataSection;
  functions.FinalizeMemory = FinalizeMemory;
  functions.Destroy = Destroy;
  return wrap(new SimpleBindingMemoryManager(functions, Opaque));
}

void LLVMDisposeMCJITMemoryManager(LLVMMCJITMemoryManagerRef MM) {
  delete unwrap(MM);
}

/*===-- JIT Event Listener functions -------------------------------------===*/

#if !LLVM_USE_INTEL_JITEVENTS
LLVMJITEventListenerRef LLVMCreateIntelJITEventListener(void) {
  return nullptr;
}
#endif

#if !LLVM_USE_OPROFILE
LLVMJITEventListenerRef LLVMCreateOProfileJITEventListener(void) {
  return nullptr;
}
#endif

#if !LLVM_USE_PERF
LLVMJITEventListenerRef LLVMCreatePerfJITEventListener(void) { return nullptr; }
#endif
