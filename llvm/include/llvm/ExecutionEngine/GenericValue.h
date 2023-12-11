//===- GenericValue.h - Represent any type of LLVM value --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The GenericValue class is used to represent an LLVM value of arbitrary type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_GENERICVALUE_H
#define LLVM_EXECUTIONENGINE_GENERICVALUE_H

#include "llvm-c/Miri.h"
#include "llvm/ADT/APInt.h"
#include <vector>

namespace llvm {

using PointerTy = void *;

struct GenericValue {
  llvm::Type *ValueTy = nullptr;
  struct IntPair {
    unsigned int first;
    unsigned int second;
  };
  union {
    double DoubleVal;
    float FloatVal;
    PointerTy PointerVal;
    struct IntPair UIntPairVal;
    unsigned char Untyped[8];
  };
  APInt IntVal; // also used for long doubles.
  MiriProvenance Provenance = {0, 0};
  std::vector<GenericValue> AggregateVal;

  // to make code faster, set GenericValue to zero could be omitted, but it is
  // potentially can cause problems, since GenericValue to store garbage
  // instead of zero.
  GenericValue() : IntVal(1, 0) {
    UIntPairVal.first = 0;
    UIntPairVal.second = 0;
    Provenance = NULL_PROVENANCE;
  }
  explicit GenericValue(MiriPointer Meta)
      : ValueTy(nullptr), PointerVal((void *)(intptr_t)Meta.addr), IntVal(1, 0),
        Provenance(Meta.prov) {}
  explicit GenericValue(void *V)
      : ValueTy(nullptr), PointerVal(V), IntVal(1, 0), Provenance(NULL_PROVENANCE) {}
};
inline GenericValue MiriPointerTOGV(MiriPointer P) { return GenericValue(P); }
inline GenericValue PTOGV(void *P) { return GenericValue(P); }
inline void *GVTOP(const GenericValue &GV) { return GV.PointerVal; }
inline MiriPointer GVTOMiriPointer(GenericValue &GV) {
  return MiriPointer{
      (uint64_t)(uintptr_t)GV.PointerVal,
      GV.Provenance,
  };
}
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_GENERICVALUE_H

