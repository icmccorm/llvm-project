//===- Miri.h --*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_C_MIRI_H
#define LLVM_C_MIRI_H
#include "llvm-c/ExecutionEngine.h"
#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"
#include <stdbool.h>
#include <stddef.h>
LLVM_C_EXTERN_C_BEGIN

typedef struct LLVMOpaqueGenericValue *LLVMGenericValueRef;
typedef struct LLVMOpaqueGenericValueArrayRef *LLVMGenericValueArrayRef;

typedef struct MiriProvenance {
  uint64_t alloc_id;
  uint64_t tag;
} MiriProvenance;

typedef struct MiriPointer {
  uint64_t addr;
  MiriProvenance prov;
} MiriPointer;

const MiriProvenance NULL_PROVENANCE = {0, 0};
typedef struct MiriErrorTrace {
  const char *directory;
  size_t directory_len;
  const char *file;
  size_t file_len;
  unsigned int line;
  unsigned int column;
} MiriErrorTrace;

typedef bool (*MiriMemset)(void *, MiriPointer, int, uint64_t);
typedef bool (*MiriMemcpy)(void *, MiriPointer, const char *, uint64_t);

typedef MiriPointer (*MiriIntToPtr)(void *, uint64_t);
typedef uint64_t (*MiriPtrToInt)(void *, MiriPointer);

typedef MiriPointer (*MiriAllocationHook)(void *, uint64_t, uint64_t);
typedef bool (*MiriFreeHook)(void *, MiriPointer);
typedef bool (*MiriLoadStoreHook)(void *, LLVMGenericValueRef, MiriPointer,
                                  LLVMTypeRef, uint64_t, uint64_t);
typedef void (*MiriStackTraceRecorderHook)(void *, MiriErrorTrace *const,
                                           uint64_t);
typedef LLVMGenericValueRef (*MiriCallByNameHook)(void *,
                                                  LLVMGenericValueArrayRef,
                                                  const char *, uint64_t,
                                                  LLVMTypeRef);
typedef LLVMGenericValueRef (*MiriCallByPointerHook)(void *, MiriPointer,
                                                     LLVMGenericValueArrayRef,
                                                     LLVMTypeRef);
LLVM_C_EXTERN_C_END
#endif // LLVM_C_MIRI_H
