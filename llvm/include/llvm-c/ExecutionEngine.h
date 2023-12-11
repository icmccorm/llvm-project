/*===-- llvm-c/ExecutionEngine.h - ExecutionEngine Lib C Iface --*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMExecutionEngine.o, which    *|
|* implements various analyses of the LLVM IR.                                *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_EXECUTIONENGINE_H
#define LLVM_C_EXECUTIONENGINE_H
#include "llvm-c/ExternC.h"
#include "llvm-c/Miri.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCExecutionEngine Execution Engine
 * @ingroup LLVMC
 *
 * @{
 */

void LLVMLinkInMCJIT(void);
void LLVMLinkInInterpreter(void);

typedef struct LLVMOpaqueExecutionEngine *LLVMExecutionEngineRef;
typedef struct LLVMOpaqueMCJITMemoryManager *LLVMMCJITMemoryManagerRef;

struct LLVMMCJITCompilerOptions {
  unsigned OptLevel;
  LLVMCodeModel CodeModel;
  LLVMBool NoFramePointerElim;
  LLVMBool EnableFastISel;
  LLVMMCJITMemoryManagerRef MCJMM;
};

/*===-- Operations on generic values --------------------------------------===*/

LLVMGenericValueRef LLVMCreateGenericValueOfData(const uint8_t *Data,
                                                 uint32_t Len);

LLVMGenericValueRef LLVMCreateGenericValueOfInt(LLVMTypeRef Ty,
                                                unsigned long long N,
                                                LLVMBool IsSigned);

LLVMGenericValueRef LLVMCreateAggregateGenericValue(uint64_t NumMembers);

void LLVMGenericValueAppendAggregateValue(LLVMGenericValueRef GenVal,
                                          LLVMGenericValueRef GenValElement);

void LLVMGenericValueEnsureCapacity(LLVMGenericValueRef GenVal,
                                    uint64_t Capacity);

LLVMGenericValueRef LLVMCreateGenericValueOfPointer(void *P);

LLVMGenericValueRef LLVMCreateGenericValueOfMiriPointer(MiriPointer Prov);

LLVMGenericValueRef LLVMCreateGenericValueOfFloat(LLVMTypeRef Ty, double N);

LLVMGenericValueRef LLVMCreateGenericValueOfFloatSingle(float N);

LLVMGenericValueRef LLVMCreateGenericValueOfFloatDouble(double N);

unsigned LLVMGenericValueIntWidth(LLVMGenericValueRef GenValRef);

void *LLVMGenericValueToPointer(LLVMGenericValueRef GenVal);

MiriPointer LLVMGenericValueToMiriPointer(LLVMGenericValueRef GenVal);

double LLVMGenericValueToFloat(LLVMTypeRef TyRef, LLVMGenericValueRef GenVal);

float LLVMGenericValueToFloatSingle(LLVMGenericValueRef GenVal);

double LLVMGenericValueToFloatDouble(LLVMGenericValueRef GenVal);

void LLVMGenericValueSetDoubleValue(LLVMGenericValueRef GenVal,
                                    double DoubleVal);

void LLVMGenericValueSetFloatValue(LLVMGenericValueRef GenVal, float FloatVal);

void LLVMGenericValueSetIntValue(LLVMGenericValueRef GenVal, uint64_t *Data,
                                 uint64_t Bytes);

APIntPointer LLVMGenericValueToInt(LLVMGenericValueRef GenVal);

void LLVMGenericValueSetMiriPointerValue(LLVMGenericValueRef GenVal,
                                         MiriPointer Ptr); 

LLVMTypeRef LLVMGenericValueGetTypeTag(LLVMGenericValueRef GenVal);

void LLVMGenericValueSetTypeTag(LLVMGenericValueRef GenVal, LLVMTypeRef Type);

void LLVMExecutionEngineInitializeConstructorDestructorLists(LLVMExecutionEngineRef EE);

uint64_t LLVMExecutionEngineGetConstructorCount(LLVMExecutionEngineRef EE);

uint64_t LLVMExecutionEngineGetDestructorCount(LLVMExecutionEngineRef EE);

LLVMValueRef LLVMExecutionEngineGetDestructorAtIndex(LLVMExecutionEngineRef EE,
                                                     uint64_t Index);

LLVMValueRef LLVMExecutionEngineGetConstructorAtIndex(LLVMExecutionEngineRef EE,
                                                      uint64_t Index);

LLVMGenericValueRef
LLVMGetPointerToAggregateGenericValue(LLVMGenericValueRef GenValRef,
                                      uint64_t Index);

size_t LLVMGetAggregateGenericValueLength(LLVMGenericValueRef GenValRef);

void LLVMDisposeGenericValue(LLVMGenericValueRef GenVal);

LLVMGenericValueRef
LLVMGenericValueArrayRefGetElementAt(LLVMGenericValueArrayRef GenArray,
                                     uint64_t Index);

uint64_t LLVMGenericValueArrayRefLength(LLVMGenericValueArrayRef GenArray);

/*===-- Operations on execution engines -----------------------------------===*/

LLVMBool LLVMCreateExecutionEngineForModule(LLVMExecutionEngineRef *OutEE,
                                            LLVMModuleRef M, char **OutError);

LLVMBool LLVMCreateInterpreterForModule(LLVMExecutionEngineRef *OutInterp,
                                        LLVMModuleRef M, char **OutError);

LLVMBool LLVMCreateJITCompilerForModule(LLVMExecutionEngineRef *OutJIT,
                                        LLVMModuleRef M, unsigned OptLevel,
                                        char **OutError);

void LLVMInitializeMCJITCompilerOptions(
    struct LLVMMCJITCompilerOptions *Options, size_t SizeOfOptions);

/**
 * Create an MCJIT execution engine for a module, with the given options. It is
 * the responsibility of the caller to ensure that all fields in Options up to
 * the given SizeOfOptions are initialized. It is correct to pass a smaller
 * value of SizeOfOptions that omits some fields. The canonical way of using
 * this is:
 *
 * LLVMMCJITCompilerOptions options;
 * LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
 * ... fill in those options you care about
 * LLVMCreateMCJITCompilerForModule(&jit, mod, &options, sizeof(options),
 *                                  &error);
 *
 * Note that this is also correct, though possibly suboptimal:
 *
 * LLVMCreateMCJITCompilerForModule(&jit, mod, 0, 0, &error);
 */
LLVMBool
LLVMCreateMCJITCompilerForModule(LLVMExecutionEngineRef *OutJIT,
                                 LLVMModuleRef M,
                                 struct LLVMMCJITCompilerOptions *Options,
                                 size_t SizeOfOptions, char **OutError);

void LLVMDisposeExecutionEngine(LLVMExecutionEngineRef EE);

void LLVMRunStaticConstructors(LLVMExecutionEngineRef EE);

void LLVMRunStaticDestructors(LLVMExecutionEngineRef EE);

int LLVMRunFunctionAsMain(LLVMExecutionEngineRef EE, LLVMValueRef F,
                          unsigned ArgC, const char *const *ArgV,
                          const char *const *EnvP);

LLVMGenericValueRef LLVMRunFunction(LLVMExecutionEngineRef EE, LLVMValueRef F,
                                    unsigned NumArgs,
                                    LLVMGenericValueRef *Args);

void LLVMFreeMachineCodeForFunction(LLVMExecutionEngineRef EE, LLVMValueRef F);

void LLVMAddModule(LLVMExecutionEngineRef EE, LLVMModuleRef M);

LLVMBool LLVMRemoveModule(LLVMExecutionEngineRef EE, LLVMModuleRef M,
                          LLVMModuleRef *OutMod, char **OutError);

LLVMBool LLVMFindFunction(LLVMExecutionEngineRef EE, const char *Name,
                          LLVMValueRef *OutFn);

void *LLVMRecompileAndRelinkFunction(LLVMExecutionEngineRef EE,
                                     LLVMValueRef Fn);

LLVMTargetDataRef LLVMGetExecutionEngineTargetData(LLVMExecutionEngineRef EE);
LLVMTargetMachineRef
LLVMGetExecutionEngineTargetMachine(LLVMExecutionEngineRef EE);

void LLVMAddGlobalMapping(LLVMExecutionEngineRef EE, LLVMValueRef Global,
                          void *Addr);

void *LLVMGetPointerToGlobal(LLVMExecutionEngineRef EE, LLVMValueRef Global);

uint64_t LLVMGetGlobalValueAddress(LLVMExecutionEngineRef EE, const char *Name);

uint64_t LLVMGetFunctionAddress(LLVMExecutionEngineRef EE, const char *Name);

/// Returns true on error, false on success. If true is returned then the error
/// message is copied to OutStr and cleared in the ExecutionEngine instance.
LLVMBool LLVMExecutionEngineGetErrMsg(LLVMExecutionEngineRef EE,
                                      char **OutError);
/*===-- Interoperation with Miri ------------------------------------------===*/

void LLVMExecutionEngineSetMiriCallByNameHook(
    LLVMExecutionEngineRef EE, MiriCallByNameHook IncomingCallbackHook);

void LLVMExecutionEngineSetMiriCallByPointerHook(
    LLVMExecutionEngineRef EE, MiriCallByPointerHook IncomingCallbackHook);

void LLVMExecutionEngineSetMiriInterpCxWrapper(LLVMExecutionEngineRef EE,
                                               void *MiriWrapper);

void LLVMExecutionEngineSetMiriLoadHook(LLVMExecutionEngineRef EE,
                                        MiriLoadStoreHook IncomingLoadHook);

void LLVMExecutionEngineSetMiriStoreHook(LLVMExecutionEngineRef EE,
                                         MiriLoadStoreHook IncomingStoreHook);

void LLVMExecutionEngineSetMiriMalloc(LLVMExecutionEngineRef EE,
                                      MiriAllocationHook IncomingMallocHook);

void LLVMExecutionEngineSetMiriFree(LLVMExecutionEngineRef EE,
                                    MiriFreeHook IncomingFreeHook);

void LLVMExecutionEngineSetMiriStackTraceRecorderHook(
    LLVMExecutionEngineRef EE,
    MiriStackTraceRecorderHook IncomingStackTraceRecorderHook);

void LLVMExecutionEngineSetMiriMemset(LLVMExecutionEngineRef EE,
                                      MiriMemset IncomingMemset);

void LLVMExecutionEngineSetMiriMemcpy(LLVMExecutionEngineRef EE,
                                      MiriMemcpy IncomingMemcpy);

void LLVMExecutionEngineSetMiriIntToPtr(LLVMExecutionEngineRef EE,
                                        MiriIntToPtr IncomingIntToPtr);

void LLVMExecutionEngineSetMiriPtrToInt(LLVMExecutionEngineRef EE,
                                        MiriPtrToInt IncomingPtrToInt);

void LLVMExecutionEngineSetMiriRegisterGlobalHook(LLVMExecutionEngineRef EE,
                                                  MiriRegisterGlobalHook Hook);

void LLVMExecutionEngineSetMiriGetElementPointerHook(
    LLVMExecutionEngineRef EE, MiriGetElementPointerHook Hook);

LLVMBool LLVMExecutionEngineStepThread(LLVMExecutionEngineRef EE,
                                       uint64_t ThreadID,
                                       LLVMGenericValueRef PendingReturnVal);

void LLVMExecutionEngineCreateThread(LLVMExecutionEngineRef EE,
                                     uint64_t ThreadID, LLVMValueRef F,
                                     unsigned NumArgs,
                                     LLVMGenericValueRef *Args);

LLVMGenericValueRef
LLVMExecutionEngineGetThreadExitValue(LLVMExecutionEngineRef EE,
                                      uint64_t ThreadID);

LLVMBool LLVMExecutionEngineHasThread(LLVMExecutionEngineRef EE,
                                      uint64_t ThreadID);

void LLVMExecutionEngineTerminateThread(LLVMExecutionEngineRef EE,
                                        uint64_t ThreadID);

/*===-- Operations on memory managers -------------------------------------===*/

typedef uint8_t *(*LLVMMemoryManagerAllocateCodeSectionCallback)(
    void *Opaque, uintptr_t Size, unsigned Alignment, unsigned SectionID,
    const char *SectionName);
typedef uint8_t *(*LLVMMemoryManagerAllocateDataSectionCallback)(
    void *Opaque, uintptr_t Size, unsigned Alignment, unsigned SectionID,
    const char *SectionName, LLVMBool IsReadOnly);
typedef LLVMBool (*LLVMMemoryManagerFinalizeMemoryCallback)(void *Opaque,
                                                            char **ErrMsg);
typedef void (*LLVMMemoryManagerDestroyCallback)(void *Opaque);

/**
 * Create a simple custom MCJIT memory manager. This memory manager can
 * intercept allocations in a module-oblivious way. This will return NULL
 * if any of the passed functions are NULL.
 *
 * @param Opaque An opaque client object to pass back to the callbacks.
 * @param AllocateCodeSection Allocate a block of memory for executable code.
 * @param AllocateDataSection Allocate a block of memory for data.
 * @param FinalizeMemory Set page permissions and flush cache. Return 0 on
 *   success, 1 on error.
 */
LLVMMCJITMemoryManagerRef LLVMCreateSimpleMCJITMemoryManager(
    void *Opaque,
    LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection,
    LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection,
    LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory,
    LLVMMemoryManagerDestroyCallback Destroy);

void LLVMDisposeMCJITMemoryManager(LLVMMCJITMemoryManagerRef MM);

/*===-- JIT Event Listener functions -------------------------------------===*/

LLVMJITEventListenerRef LLVMCreateGDBRegistrationListener(void);
LLVMJITEventListenerRef LLVMCreateIntelJITEventListener(void);
LLVMJITEventListenerRef LLVMCreateOProfileJITEventListener(void);
LLVMJITEventListenerRef LLVMCreatePerfJITEventListener(void);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
