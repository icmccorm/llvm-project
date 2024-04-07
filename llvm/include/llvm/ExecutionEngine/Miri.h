//===- Miri.h ----------------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_MIRI_H
#define LLVM_EXECUTIONENGINE_MIRI_H
#include "llvm-c/Miri.h"

inline bool operator==(MiriPointer const &lhs, MiriPointer const &rhs) {
  return lhs.addr == rhs.addr && lhs.prov.alloc_id == rhs.prov.alloc_id &&
         lhs.prov.tag == rhs.prov.tag;
}
const uint32_t MIRI_POINTER_BIT_WIDTH = 64;

#endif