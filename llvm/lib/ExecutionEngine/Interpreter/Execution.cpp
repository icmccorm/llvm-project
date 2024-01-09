//===-- Execution.cpp - Implement code to simulate the program ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains the actual instruction interpreter.
//
//===----------------------------------------------------------------------===//

#include "Interpreter.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace std;
using namespace llvm;

#define DEBUG_TYPE "interpreter"

STATISTIC(NumDynamicInsts, "Number of dynamic instructions executed");

static cl::opt<bool> PrintVolatile(
    "interpreter-print-volatile", cl::Hidden,
    cl::desc("make the interpreter print every volatile load and store"));

//===----------------------------------------------------------------------===//
//                     Various Helper Functions
//===----------------------------------------------------------------------===//

static void SetValue(Value *V, GenericValue Val, ExecutionContext &SF) {
  Val.ValueTy = V->getType();
  SF.Values[V] = Val;
}

static std::string type_to_string(Type *Ty) {
  std::string TypeString;
  llvm::raw_string_ostream TypeStream(TypeString);
  Ty->print(TypeStream);
  return TypeString;
}

static std::string inst_to_string(Instruction *I) {
  std::string InstString;
  llvm::raw_string_ostream InstStream(InstString);
  I->print(InstStream);
  return InstString;
}

//===----------------------------------------------------------------------===//
//                    Unary Instruction Implementations
//===----------------------------------------------------------------------===//

static void executeFNegInst(GenericValue &Dest, GenericValue Src, Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::FloatTyID:
    Dest.FloatVal = -Src.FloatVal;
    break;
  case Type::DoubleTyID:
    Dest.DoubleVal = -Src.DoubleVal;
    break;
  default:
    std::string Message =
        "Unhandled type for ICMP_UGT predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
}

void Interpreter::visitUnaryOperator(UnaryOperator &I) {
  ExecutionContext &SF = Interpreter::context();
  Type *Ty = I.getOperand(0)->getType();
  GenericValue Src = getOperandValue(I.getOperand(0), SF);
  GenericValue R; // Result

  // First process vector operation
  if (Ty->isVectorTy()) {
    R.AggregateVal.resize(Src.AggregateVal.size());

    switch (I.getOpcode()) {
    default:
      report_fatal_error("Invalid unary operator");
      break;
    case Instruction::FNeg:
      if (cast<VectorType>(Ty)->getElementType()->isFloatTy()) {
        for (unsigned i = 0; i < R.AggregateVal.size(); ++i)
          R.AggregateVal[i].FloatVal = -Src.AggregateVal[i].FloatVal;
      } else if (cast<VectorType>(Ty)->getElementType()->isDoubleTy()) {
        for (unsigned i = 0; i < R.AggregateVal.size(); ++i)
          R.AggregateVal[i].DoubleVal = -Src.AggregateVal[i].DoubleVal;
      } else {
        std::string Message =
            "Unhandled type for Fneg instruction: " + type_to_string(Ty);
        report_fatal_error(Message.c_str());
      }
      break;
    }
  } else {
    switch (I.getOpcode()) {
    default:
      report_fatal_error("Invalid unary operator");
      break;
    case Instruction::FNeg:
      executeFNegInst(R, Src, Ty);
      break;
    }
  }
  SetValue(&I, R, SF);
}

//===----------------------------------------------------------------------===//
//                    Binary Instruction Implementations
//===----------------------------------------------------------------------===//

#define IMPLEMENT_BINARY_OPERATOR(OP, TY)                                      \
  case Type::TY##TyID:                                                         \
    Dest.TY##Val = Src1.TY##Val OP Src2.TY##Val;                               \
    break

static void executeFAddInst(GenericValue &Dest, GenericValue Src1,
                            GenericValue Src2, Type *Ty) {
  switch (Ty->getTypeID()) {
    IMPLEMENT_BINARY_OPERATOR(+, Float);
    IMPLEMENT_BINARY_OPERATOR(+, Double);
  default:
    std::string Message =
        "Unhandled type for FAdd predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
}

static void executeFSubInst(GenericValue &Dest, GenericValue Src1,
                            GenericValue Src2, Type *Ty) {
  switch (Ty->getTypeID()) {
    IMPLEMENT_BINARY_OPERATOR(-, Float);
    IMPLEMENT_BINARY_OPERATOR(-, Double);
  default:
    std::string Message =
        "Unhandled type for FSub instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
}

static void executeFMulInst(GenericValue &Dest, GenericValue Src1,
                            GenericValue Src2, Type *Ty) {
  switch (Ty->getTypeID()) {
    IMPLEMENT_BINARY_OPERATOR(*, Float);
    IMPLEMENT_BINARY_OPERATOR(*, Double);
  default:
    std::string Message =
        "Unhandled type for FMul instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
}

static void executeFDivInst(GenericValue &Dest, GenericValue Src1,
                            GenericValue Src2, Type *Ty) {
  switch (Ty->getTypeID()) {
    IMPLEMENT_BINARY_OPERATOR(/, Float);
    IMPLEMENT_BINARY_OPERATOR(/, Double);
  default:
    std::string Message =
        "Unhandled type for FDiv instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
}

static void executeFRemInst(GenericValue &Dest, GenericValue Src1,
                            GenericValue Src2, Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::FloatTyID:
    Dest.FloatVal = fmod(Src1.FloatVal, Src2.FloatVal);
    break;
  case Type::DoubleTyID:
    Dest.DoubleVal = fmod(Src1.DoubleVal, Src2.DoubleVal);
    break;
  default:
    std::string Message =
        "Unhandled type for Rem instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
}

#define IMPLEMENT_INTEGER_ICMP(OP, TY)                                         \
  case Type::IntegerTyID:                                                      \
    Dest.IntVal = APInt(1, Src1.IntVal.OP(Src2.IntVal));                       \
    break;

#define IMPLEMENT_VECTOR_INTEGER_ICMP(OP, TY)                                  \
  case Type::FixedVectorTyID:                                                  \
  case Type::ScalableVectorTyID: {                                             \
    assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());              \
    Dest.AggregateVal.resize(Src1.AggregateVal.size());                        \
    for (uint32_t _i = 0; _i < Src1.AggregateVal.size(); _i++)                 \
      Dest.AggregateVal[_i].IntVal = APInt(                                    \
          1, Src1.AggregateVal[_i].IntVal.OP(Src2.AggregateVal[_i].IntVal));   \
  } break;

// Handle pointers specially because they must be compared with only as much
// width as the host has.  We _do not_ want to be comparing 64 bit values when
// running on a 32-bit target, otherwise the upper 32 bits might mess up
// comparisons if they contain garbage.
#define IMPLEMENT_POINTER_ICMP(OP)                                             \
  case Type::PointerTyID:                                                      \
    Dest.IntVal =                                                              \
        APInt(1, (void *)(intptr_t)Src1.PointerVal OP(void *)(intptr_t)        \
                     Src2.PointerVal);                                         \
    break;

static GenericValue executeICMP_EQ(GenericValue Src1, GenericValue Src2,
                                   Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(eq, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(eq, Ty);
    IMPLEMENT_POINTER_ICMP(==);
  default:
    std::string Message =
        "Unhandled type for ICMP_EQ predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_NE(GenericValue Src1, GenericValue Src2,
                                   Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(ne, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(ne, Ty);
    IMPLEMENT_POINTER_ICMP(!=);
  default:
    std::string Message =
        "Unhandled type for ICMP_NE predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_ULT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(ult, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(ult, Ty);
    IMPLEMENT_POINTER_ICMP(<);
  default:
    std::string Message =
        "Unhandled type for ICMP_ULT predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_SLT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(slt, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(slt, Ty);
    IMPLEMENT_POINTER_ICMP(<);
  default:
    std::string Message =
        "Unhandled type for ICMP_SLT predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_UGT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(ugt, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(ugt, Ty);
    IMPLEMENT_POINTER_ICMP(>);
  default:
    std::string Message =
        "Unhandled type for ICMP_UGT predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_SGT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(sgt, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(sgt, Ty);
    IMPLEMENT_POINTER_ICMP(>);
  default:
    std::string Message =
        "Unhandled type for ICMP_SGT predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_ULE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(ule, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(ule, Ty);
    IMPLEMENT_POINTER_ICMP(<=);
  default:
    std::string Message =
        "Unhandled type for ICMP_ULE predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_SLE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(sle, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(sle, Ty);
    IMPLEMENT_POINTER_ICMP(<=);
  default:
    std::string Message =
        "Unhandled type for ICMP_SLE predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_UGE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(uge, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(uge, Ty);
    IMPLEMENT_POINTER_ICMP(>=);
  default:
    std::string Message =
        "Unhandled type for ICMP_UGE predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeICMP_SGE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_INTEGER_ICMP(sge, Ty);
    IMPLEMENT_VECTOR_INTEGER_ICMP(sge, Ty);
    IMPLEMENT_POINTER_ICMP(>=);
  default:
    std::string Message =
        "Unhandled type for ICMP_SGE predicate: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

void Interpreter::visitICmpInst(ICmpInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Type *Ty = I.getOperand(0)->getType();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue R; // Result

  switch (I.getPredicate()) {
  case ICmpInst::ICMP_EQ:
    R = executeICMP_EQ(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_NE:
    R = executeICMP_NE(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_ULT:
    R = executeICMP_ULT(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_SLT:
    R = executeICMP_SLT(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_UGT:
    R = executeICMP_UGT(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_SGT:
    R = executeICMP_SGT(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_ULE:
    R = executeICMP_ULE(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_SLE:
    R = executeICMP_SLE(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_UGE:
    R = executeICMP_UGE(Src1, Src2, Ty);
    break;
  case ICmpInst::ICMP_SGE:
    R = executeICMP_SGE(Src1, Src2, Ty);
    break;
  default: {
    std::string Message = "Unknown ICmp predicate: " + inst_to_string(&I);
    report_fatal_error(Message.c_str());
  } break;
  }

  SetValue(&I, R, SF);
}

#define IMPLEMENT_FCMP(OP, TY)                                                 \
  case Type::TY##TyID:                                                         \
    Dest.IntVal = APInt(1, Src1.TY##Val OP Src2.TY##Val);                      \
    break

#define IMPLEMENT_VECTOR_FCMP_T(OP, TY)                                        \
  assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());                \
  Dest.AggregateVal.resize(Src1.AggregateVal.size());                          \
  for (uint32_t _i = 0; _i < Src1.AggregateVal.size(); _i++)                   \
    Dest.AggregateVal[_i].IntVal = APInt(                                      \
        1, Src1.AggregateVal[_i].TY##Val OP Src2.AggregateVal[_i].TY##Val);    \
  break;

#define IMPLEMENT_VECTOR_FCMP(OP)                                              \
  case Type::FixedVectorTyID:                                                  \
  case Type::ScalableVectorTyID:                                               \
    if (cast<VectorType>(Ty)->getElementType()->isFloatTy()) {                 \
      IMPLEMENT_VECTOR_FCMP_T(OP, Float);                                      \
    } else {                                                                   \
      IMPLEMENT_VECTOR_FCMP_T(OP, Double);                                     \
    }

static GenericValue executeFCMP_OEQ(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_FCMP(==, Float);
    IMPLEMENT_FCMP(==, Double);
    IMPLEMENT_VECTOR_FCMP(==);
  default:
    std::string Message =
        "Unhandled type for FCmp EQ instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

#define IMPLEMENT_SCALAR_NANS(TY, X, Y)                                        \
  if (TY->isFloatTy()) {                                                       \
    if (X.FloatVal != X.FloatVal || Y.FloatVal != Y.FloatVal) {                \
      Dest.IntVal = APInt(1, false);                                           \
      return Dest;                                                             \
    }                                                                          \
  } else {                                                                     \
    if (X.DoubleVal != X.DoubleVal || Y.DoubleVal != Y.DoubleVal) {            \
      Dest.IntVal = APInt(1, false);                                           \
      return Dest;                                                             \
    }                                                                          \
  }

#define MASK_VECTOR_NANS_T(X, Y, TZ, FLAG)                                     \
  assert(X.AggregateVal.size() == Y.AggregateVal.size());                      \
  Dest.AggregateVal.resize(X.AggregateVal.size());                             \
  for (uint32_t _i = 0; _i < X.AggregateVal.size(); _i++) {                    \
    if (X.AggregateVal[_i].TZ##Val != X.AggregateVal[_i].TZ##Val ||            \
        Y.AggregateVal[_i].TZ##Val != Y.AggregateVal[_i].TZ##Val)              \
      Dest.AggregateVal[_i].IntVal = APInt(1, FLAG);                           \
    else {                                                                     \
      Dest.AggregateVal[_i].IntVal = APInt(1, !FLAG);                          \
    }                                                                          \
  }

#define MASK_VECTOR_NANS(TY, X, Y, FLAG)                                       \
  if (TY->isVectorTy()) {                                                      \
    if (cast<VectorType>(TY)->getElementType()->isFloatTy()) {                 \
      MASK_VECTOR_NANS_T(X, Y, Float, FLAG)                                    \
    } else {                                                                   \
      MASK_VECTOR_NANS_T(X, Y, Double, FLAG)                                   \
    }                                                                          \
  }

static GenericValue executeFCMP_ONE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  // if input is scalar value and Src1 or Src2 is NaN return false
  IMPLEMENT_SCALAR_NANS(Ty, Src1, Src2)
  // if vector input detect NaNs and fill mask
  MASK_VECTOR_NANS(Ty, Src1, Src2, false)
  GenericValue DestMask = Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_FCMP(!=, Float);
    IMPLEMENT_FCMP(!=, Double);
    IMPLEMENT_VECTOR_FCMP(!=);
  default:
    std::string Message =
        "Unhandled type for FCmp NE instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  // in vector case mask out NaN elements
  if (Ty->isVectorTy())
    for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)
      if (DestMask.AggregateVal[_i].IntVal == false)
        Dest.AggregateVal[_i].IntVal = APInt(1, false);

  return Dest;
}

static GenericValue executeFCMP_OLE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_FCMP(<=, Float);
    IMPLEMENT_FCMP(<=, Double);
    IMPLEMENT_VECTOR_FCMP(<=);
  default:
    std::string Message =
        "Unhandled type for FCmp LE instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeFCMP_OGE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_FCMP(>=, Float);
    IMPLEMENT_FCMP(>=, Double);
    IMPLEMENT_VECTOR_FCMP(>=);
  default:
    std::string Message =
        "Unhandled type for FCmp GE instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeFCMP_OLT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_FCMP(<, Float);
    IMPLEMENT_FCMP(<, Double);
    IMPLEMENT_VECTOR_FCMP(<);
  default:
    std::string Message =
        "Unhandled type for FCmp LT instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

static GenericValue executeFCMP_OGT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
    IMPLEMENT_FCMP(>, Float);
    IMPLEMENT_FCMP(>, Double);
    IMPLEMENT_VECTOR_FCMP(>);
  default:
    std::string Message =
        "Unhandled type for FCmp GT instruction: " + type_to_string(Ty);
    report_fatal_error(Message.c_str());
  }
  return Dest;
}

#define IMPLEMENT_UNORDERED(TY, X, Y)                                          \
  if (TY->isFloatTy()) {                                                       \
    if (X.FloatVal != X.FloatVal || Y.FloatVal != Y.FloatVal) {                \
      Dest.IntVal = APInt(1, true);                                            \
      return Dest;                                                             \
    }                                                                          \
  } else if (X.DoubleVal != X.DoubleVal || Y.DoubleVal != Y.DoubleVal) {       \
    Dest.IntVal = APInt(1, true);                                              \
    return Dest;                                                               \
  }

#define IMPLEMENT_VECTOR_UNORDERED(TY, X, Y, FUNC)                             \
  if (TY->isVectorTy()) {                                                      \
    GenericValue DestMask = Dest;                                              \
    Dest = FUNC(Src1, Src2, Ty);                                               \
    for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)                   \
      if (DestMask.AggregateVal[_i].IntVal == true)                            \
        Dest.AggregateVal[_i].IntVal = APInt(1, true);                         \
    return Dest;                                                               \
  }

static GenericValue executeFCMP_UEQ(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  IMPLEMENT_UNORDERED(Ty, Src1, Src2)
  MASK_VECTOR_NANS(Ty, Src1, Src2, true)
  IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OEQ)
  return executeFCMP_OEQ(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UNE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  IMPLEMENT_UNORDERED(Ty, Src1, Src2)
  MASK_VECTOR_NANS(Ty, Src1, Src2, true)
  IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_ONE)
  return executeFCMP_ONE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ULE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  IMPLEMENT_UNORDERED(Ty, Src1, Src2)
  MASK_VECTOR_NANS(Ty, Src1, Src2, true)
  IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OLE)
  return executeFCMP_OLE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UGE(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  IMPLEMENT_UNORDERED(Ty, Src1, Src2)
  MASK_VECTOR_NANS(Ty, Src1, Src2, true)
  IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OGE)
  return executeFCMP_OGE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ULT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  IMPLEMENT_UNORDERED(Ty, Src1, Src2)
  MASK_VECTOR_NANS(Ty, Src1, Src2, true)
  IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OLT)
  return executeFCMP_OLT(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UGT(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  IMPLEMENT_UNORDERED(Ty, Src1, Src2)
  MASK_VECTOR_NANS(Ty, Src1, Src2, true)
  IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OGT)
  return executeFCMP_OGT(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ORD(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  if (Ty->isVectorTy()) {
    assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
    Dest.AggregateVal.resize(Src1.AggregateVal.size());
    if (cast<VectorType>(Ty)->getElementType()->isFloatTy()) {
      for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)
        Dest.AggregateVal[_i].IntVal =
            APInt(1, ((Src1.AggregateVal[_i].FloatVal ==
                       Src1.AggregateVal[_i].FloatVal) &&
                      (Src2.AggregateVal[_i].FloatVal ==
                       Src2.AggregateVal[_i].FloatVal)));
    } else {
      for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)
        Dest.AggregateVal[_i].IntVal =
            APInt(1, ((Src1.AggregateVal[_i].DoubleVal ==
                       Src1.AggregateVal[_i].DoubleVal) &&
                      (Src2.AggregateVal[_i].DoubleVal ==
                       Src2.AggregateVal[_i].DoubleVal)));
    }
  } else if (Ty->isFloatTy())
    Dest.IntVal = APInt(
        1, (Src1.FloatVal == Src1.FloatVal && Src2.FloatVal == Src2.FloatVal));
  else {
    Dest.IntVal = APInt(1, (Src1.DoubleVal == Src1.DoubleVal &&
                            Src2.DoubleVal == Src2.DoubleVal));
  }
  return Dest;
}

static GenericValue executeFCMP_UNO(GenericValue Src1, GenericValue Src2,
                                    Type *Ty) {
  GenericValue Dest;
  if (Ty->isVectorTy()) {
    assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
    Dest.AggregateVal.resize(Src1.AggregateVal.size());
    if (cast<VectorType>(Ty)->getElementType()->isFloatTy()) {
      for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)
        Dest.AggregateVal[_i].IntVal =
            APInt(1, ((Src1.AggregateVal[_i].FloatVal !=
                       Src1.AggregateVal[_i].FloatVal) ||
                      (Src2.AggregateVal[_i].FloatVal !=
                       Src2.AggregateVal[_i].FloatVal)));
    } else {
      for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)
        Dest.AggregateVal[_i].IntVal =
            APInt(1, ((Src1.AggregateVal[_i].DoubleVal !=
                       Src1.AggregateVal[_i].DoubleVal) ||
                      (Src2.AggregateVal[_i].DoubleVal !=
                       Src2.AggregateVal[_i].DoubleVal)));
    }
  } else if (Ty->isFloatTy())
    Dest.IntVal = APInt(
        1, (Src1.FloatVal != Src1.FloatVal || Src2.FloatVal != Src2.FloatVal));
  else {
    Dest.IntVal = APInt(1, (Src1.DoubleVal != Src1.DoubleVal ||
                            Src2.DoubleVal != Src2.DoubleVal));
  }
  return Dest;
}

static GenericValue executeFCMP_BOOL(GenericValue Src1, GenericValue Src2,
                                     Type *Ty, const bool val) {
  GenericValue Dest;
  if (Ty->isVectorTy()) {
    assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
    Dest.AggregateVal.resize(Src1.AggregateVal.size());
    for (size_t _i = 0; _i < Src1.AggregateVal.size(); _i++)
      Dest.AggregateVal[_i].IntVal = APInt(1, val);
  } else {
    Dest.IntVal = APInt(1, val);
  }

  return Dest;
}

void Interpreter::visitFCmpInst(FCmpInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Type *Ty = I.getOperand(0)->getType();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue R; // Result

  switch (I.getPredicate()) {
  default: {
    std::string Message = "Unknown FCmp predicate: " + inst_to_string(&I);
    report_fatal_error(Message.c_str());
  } break;
  case FCmpInst::FCMP_FALSE:
    R = executeFCMP_BOOL(Src1, Src2, Ty, false);
    break;
  case FCmpInst::FCMP_TRUE:
    R = executeFCMP_BOOL(Src1, Src2, Ty, true);
    break;
  case FCmpInst::FCMP_ORD:
    R = executeFCMP_ORD(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_UNO:
    R = executeFCMP_UNO(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_UEQ:
    R = executeFCMP_UEQ(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_OEQ:
    R = executeFCMP_OEQ(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_UNE:
    R = executeFCMP_UNE(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_ONE:
    R = executeFCMP_ONE(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_ULT:
    R = executeFCMP_ULT(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_OLT:
    R = executeFCMP_OLT(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_UGT:
    R = executeFCMP_UGT(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_OGT:
    R = executeFCMP_OGT(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_ULE:
    R = executeFCMP_ULE(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_OLE:
    R = executeFCMP_OLE(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_UGE:
    R = executeFCMP_UGE(Src1, Src2, Ty);
    break;
  case FCmpInst::FCMP_OGE:
    R = executeFCMP_OGE(Src1, Src2, Ty);
    break;
  }

  SetValue(&I, R, SF);
}

static GenericValue executeCmpInst(unsigned predicate, GenericValue Src1,
                                   GenericValue Src2, Type *Ty) {
  GenericValue Result;
  switch (predicate) {
  case ICmpInst::ICMP_EQ:
    return executeICMP_EQ(Src1, Src2, Ty);
  case ICmpInst::ICMP_NE:
    return executeICMP_NE(Src1, Src2, Ty);
  case ICmpInst::ICMP_UGT:
    return executeICMP_UGT(Src1, Src2, Ty);
  case ICmpInst::ICMP_SGT:
    return executeICMP_SGT(Src1, Src2, Ty);
  case ICmpInst::ICMP_ULT:
    return executeICMP_ULT(Src1, Src2, Ty);
  case ICmpInst::ICMP_SLT:
    return executeICMP_SLT(Src1, Src2, Ty);
  case ICmpInst::ICMP_UGE:
    return executeICMP_UGE(Src1, Src2, Ty);
  case ICmpInst::ICMP_SGE:
    return executeICMP_SGE(Src1, Src2, Ty);
  case ICmpInst::ICMP_ULE:
    return executeICMP_ULE(Src1, Src2, Ty);
  case ICmpInst::ICMP_SLE:
    return executeICMP_SLE(Src1, Src2, Ty);
  case FCmpInst::FCMP_ORD:
    return executeFCMP_ORD(Src1, Src2, Ty);
  case FCmpInst::FCMP_UNO:
    return executeFCMP_UNO(Src1, Src2, Ty);
  case FCmpInst::FCMP_OEQ:
    return executeFCMP_OEQ(Src1, Src2, Ty);
  case FCmpInst::FCMP_UEQ:
    return executeFCMP_UEQ(Src1, Src2, Ty);
  case FCmpInst::FCMP_ONE:
    return executeFCMP_ONE(Src1, Src2, Ty);
  case FCmpInst::FCMP_UNE:
    return executeFCMP_UNE(Src1, Src2, Ty);
  case FCmpInst::FCMP_OLT:
    return executeFCMP_OLT(Src1, Src2, Ty);
  case FCmpInst::FCMP_ULT:
    return executeFCMP_ULT(Src1, Src2, Ty);
  case FCmpInst::FCMP_OGT:
    return executeFCMP_OGT(Src1, Src2, Ty);
  case FCmpInst::FCMP_UGT:
    return executeFCMP_UGT(Src1, Src2, Ty);
  case FCmpInst::FCMP_OLE:
    return executeFCMP_OLE(Src1, Src2, Ty);
  case FCmpInst::FCMP_ULE:
    return executeFCMP_ULE(Src1, Src2, Ty);
  case FCmpInst::FCMP_OGE:
    return executeFCMP_OGE(Src1, Src2, Ty);
  case FCmpInst::FCMP_UGE:
    return executeFCMP_UGE(Src1, Src2, Ty);
  case FCmpInst::FCMP_FALSE:
    return executeFCMP_BOOL(Src1, Src2, Ty, false);
  case FCmpInst::FCMP_TRUE:
    return executeFCMP_BOOL(Src1, Src2, Ty, true);
  default: {
    std::string Message = "Unknown Cmp predicate: " + std::to_string(predicate);
    report_fatal_error(Message.c_str());
  }
  }
}

void Interpreter::visitBinaryOperator(BinaryOperator &I) {
  ExecutionContext &SF = Interpreter::context();
  Type *Ty = I.getOperand(0)->getType();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue R; // Result

  // First process vector operation
  if (Ty->isVectorTy()) {
    assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
    R.AggregateVal.resize(Src1.AggregateVal.size());

    // Macros to execute binary operation 'OP' over integer vectors
#define INTEGER_VECTOR_OPERATION(OP)                                           \
  for (unsigned i = 0; i < R.AggregateVal.size(); ++i)                         \
    R.AggregateVal[i].IntVal =                                                 \
        Src1.AggregateVal[i].IntVal OP Src2.AggregateVal[i].IntVal;

    // Additional macros to execute binary operations udiv/sdiv/urem/srem since
    // they have different notation.
#define INTEGER_VECTOR_FUNCTION(OP)                                            \
  for (unsigned i = 0; i < R.AggregateVal.size(); ++i)                         \
    R.AggregateVal[i].IntVal =                                                 \
        Src1.AggregateVal[i].IntVal.OP(Src2.AggregateVal[i].IntVal);

    // Macros to execute binary operation 'OP' over floating point type TY
    // (float or double) vectors
#define FLOAT_VECTOR_FUNCTION(OP, TY)                                          \
  for (unsigned i = 0; i < R.AggregateVal.size(); ++i)                         \
    R.AggregateVal[i].TY = Src1.AggregateVal[i].TY OP Src2.AggregateVal[i].TY;

    // Macros to choose appropriate TY: float or double and run operation
    // execution
#define FLOAT_VECTOR_OP(OP)                                                    \
  {                                                                            \
    if (cast<VectorType>(Ty)->getElementType()->isFloatTy())                   \
      FLOAT_VECTOR_FUNCTION(OP, FloatVal)                                      \
    else {                                                                     \
      if (cast<VectorType>(Ty)->getElementType()->isDoubleTy())                \
        FLOAT_VECTOR_FUNCTION(OP, DoubleVal)                                   \
      else {                                                                   \
        std::string Message =                                                  \
            "Unhandled type for OP instruction: " + type_to_string(Ty);        \
        report_fatal_error(Message.c_str());                                   \
      }                                                                        \
    }                                                                          \
  }

    switch (I.getOpcode()) {
    default: {
      std::string Message = "Unknown binary operator: " + inst_to_string(&I);
      report_fatal_error(Message.c_str());
    } break;
    case Instruction::Add:
      INTEGER_VECTOR_OPERATION(+) break;
    case Instruction::Sub:
      INTEGER_VECTOR_OPERATION(-) break;
    case Instruction::Mul:
      INTEGER_VECTOR_OPERATION(*) break;
    case Instruction::UDiv:
      INTEGER_VECTOR_FUNCTION(udiv) break;
    case Instruction::SDiv:
      INTEGER_VECTOR_FUNCTION(sdiv) break;
    case Instruction::URem:
      INTEGER_VECTOR_FUNCTION(urem) break;
    case Instruction::SRem:
      INTEGER_VECTOR_FUNCTION(srem) break;
    case Instruction::And:
      INTEGER_VECTOR_OPERATION(&) break;
    case Instruction::Or:
      INTEGER_VECTOR_OPERATION(|) break;
    case Instruction::Xor:
      INTEGER_VECTOR_OPERATION(^) break;
    case Instruction::FAdd:
      FLOAT_VECTOR_OP(+) break;
    case Instruction::FSub:
      FLOAT_VECTOR_OP(-) break;
    case Instruction::FMul:
      FLOAT_VECTOR_OP(*) break;
    case Instruction::FDiv:
      FLOAT_VECTOR_OP(/) break;
    case Instruction::FRem:
      if (cast<VectorType>(Ty)->getElementType()->isFloatTy())
        for (unsigned i = 0; i < R.AggregateVal.size(); ++i)
          R.AggregateVal[i].FloatVal = fmod(Src1.AggregateVal[i].FloatVal,
                                            Src2.AggregateVal[i].FloatVal);
      else {
        if (cast<VectorType>(Ty)->getElementType()->isDoubleTy())
          for (unsigned i = 0; i < R.AggregateVal.size(); ++i)
            R.AggregateVal[i].DoubleVal = fmod(Src1.AggregateVal[i].DoubleVal,
                                               Src2.AggregateVal[i].DoubleVal);
        else {
          std::string Message =
              "Unhandled type for Rem instruction: " + type_to_string(Ty);
          report_fatal_error(Message.c_str());
        }
      }
      break;
    }
  } else {
    switch (I.getOpcode()) {
    default: {
      std::string Message = "Unknown binary operator: " + inst_to_string(&I);
      report_fatal_error(Message.c_str());
    } break;
    case Instruction::Add:
      R.IntVal = Src1.IntVal + Src2.IntVal;
      break;
    case Instruction::Sub:
      R.IntVal = Src1.IntVal - Src2.IntVal;
      break;
    case Instruction::Mul:
      R.IntVal = Src1.IntVal * Src2.IntVal;
      break;
    case Instruction::FAdd:
      executeFAddInst(R, Src1, Src2, Ty);
      break;
    case Instruction::FSub:
      executeFSubInst(R, Src1, Src2, Ty);
      break;
    case Instruction::FMul:
      executeFMulInst(R, Src1, Src2, Ty);
      break;
    case Instruction::FDiv:
      executeFDivInst(R, Src1, Src2, Ty);
      break;
    case Instruction::FRem:
      executeFRemInst(R, Src1, Src2, Ty);
      break;
    case Instruction::UDiv:
      R.IntVal = Src1.IntVal.udiv(Src2.IntVal);
      break;
    case Instruction::SDiv:
      R.IntVal = Src1.IntVal.sdiv(Src2.IntVal);
      break;
    case Instruction::URem:
      R.IntVal = Src1.IntVal.urem(Src2.IntVal);
      break;
    case Instruction::SRem:
      R.IntVal = Src1.IntVal.srem(Src2.IntVal);
      break;
    case Instruction::And:
      R.IntVal = Src1.IntVal & Src2.IntVal;
      break;
    case Instruction::Or:
      R.IntVal = Src1.IntVal | Src2.IntVal;
      break;
    case Instruction::Xor:
      R.IntVal = Src1.IntVal ^ Src2.IntVal;
      break;
    }
  }
  SetValue(&I, R, SF);
}

static GenericValue executeSelectInst(GenericValue Src1, GenericValue Src2,
                                      GenericValue Src3, Type *Ty) {
  GenericValue Dest;
  if (Ty->isVectorTy()) {
    assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
    assert(Src2.AggregateVal.size() == Src3.AggregateVal.size());
    Dest.AggregateVal.resize(Src1.AggregateVal.size());
    for (size_t i = 0; i < Src1.AggregateVal.size(); ++i)
      Dest.AggregateVal[i] = (Src1.AggregateVal[i].IntVal == 0)
                                 ? Src3.AggregateVal[i]
                                 : Src2.AggregateVal[i];
  } else {
    Dest = (Src1.IntVal == 0) ? Src3 : Src2;
  }
  return Dest;
}

void Interpreter::visitSelectInst(SelectInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Type *Ty = I.getOperand(0)->getType();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
  GenericValue R = executeSelectInst(Src1, Src2, Src3, Ty);
  SetValue(&I, R, SF);
}

//===----------------------------------------------------------------------===//
//                     Terminator Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::exitCalled(GenericValue GV) {
  // runAtExitHandlers() assumes there are no stack frames, but
  // if exit() was called, then it had a stack frame. Blow away
  // the stack before interpreting atexit handlers.
  // TODO-MIRI: clear all paths
  runAtExitHandlers();
  exit(GV.IntVal.zextOrTrunc(32).getZExtValue());
}

/// Pop the last stack frame off of ECStack and then copy the result
/// back into the result variable if we are not returning void. The
/// result variable may be the ExitValue, or the Value of the calling
/// CallInst if there was a previous stack frame. This method may
/// invalidate any ECStack iterators you have. This method also takes
/// care of switching to the normal destination BB, if we are returning
/// from an invoke.
///
void Interpreter::popStackAndReturnValueToCaller(Type *RetTy,
                                                 GenericValue Result) {
  // Pop the current stack frame.
  Interpreter::popContext();

  passReturnValueToLowerStackFrame(RetTy, Result);
}

void Interpreter::passReturnValueToLowerStackFrame(Type *RetTy,
                                                   GenericValue Result) {
  if (Interpreter::stackIsEmpty()) {   // Finished main.  Put result into exit
                                       // code...
    if (RetTy && !RetTy->isVoidTy()) { // Nonvoid return type?
      Interpreter::setExitValue(
          Result); // Capture the exit value of the program
    } else {
      GenericValue *Exit = Interpreter::getThreadExitValue();
      memset(Exit->Untyped, 0, sizeof(Exit->Untyped));
    }
  } else {
    // If we have a previous stack frame, and we have a previous call,
    // fill in the return value...
    ExecutionContext &CallingSF = Interpreter::context();
    if (CallingSF.Caller) {
      // Save result...
      if (!CallingSF.Caller->getType()->isVoidTy())
        SetValue(CallingSF.Caller, Result, CallingSF);
      if (InvokeInst *II = dyn_cast<InvokeInst>(CallingSF.Caller))
        SwitchToNewBasicBlock(II->getNormalDest(), CallingSF);
      CallingSF.Caller = nullptr; // We returned from the call...
    }
  }
}

void Interpreter::visitReturnInst(ReturnInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Type *RetTy = Type::getVoidTy(I.getContext());
  GenericValue Result;

  // Save away the return value... (if we are not 'ret void')
  if (I.getNumOperands()) {
    RetTy = I.getReturnValue()->getType();
    Result = getOperandValue(I.getReturnValue(), SF);
  }

  popStackAndReturnValueToCaller(RetTy, Result);
}

void Interpreter::visitUnreachableInst(UnreachableInst &I) {
  report_fatal_error("Program executed an 'unreachable' instruction!");
}

void Interpreter::visitBranchInst(BranchInst &I) {
  ExecutionContext &SF = Interpreter::context();
  BasicBlock *Dest;

  Dest = I.getSuccessor(0); // Uncond branches have a fixed dest...
  if (!I.isUnconditional()) {
    Value *Cond = I.getCondition();
    if (getOperandValue(Cond, SF).IntVal == 0) // If false cond...
      Dest = I.getSuccessor(1);
  }
  SwitchToNewBasicBlock(Dest, SF);
}

void Interpreter::visitSwitchInst(SwitchInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Value *Cond = I.getCondition();
  Type *ElTy = Cond->getType();
  GenericValue CondVal = getOperandValue(Cond, SF);

  // Check to see if any of the cases match...
  BasicBlock *Dest = nullptr;
  for (auto Case : I.cases()) {
    GenericValue CaseVal = getOperandValue(Case.getCaseValue(), SF);
    if (executeICMP_EQ(CondVal, CaseVal, ElTy).IntVal != 0) {
      Dest = cast<BasicBlock>(Case.getCaseSuccessor());
      break;
    }
  }
  if (!Dest)
    Dest = I.getDefaultDest(); // No cases matched: use default
  SwitchToNewBasicBlock(Dest, SF);
}

void Interpreter::visitIndirectBrInst(IndirectBrInst &I) {
  ExecutionContext &SF = Interpreter::context();
  void *Dest = GVTOP(getOperandValue(I.getAddress(), SF));
  SwitchToNewBasicBlock((BasicBlock *)Dest, SF);
}
// SwitchToNewBasicBlock - This method is used to jump to a new basic block.
// This function handles the actual updating of block and instruction iterators
// as well as execution of all of the PHI nodes in the destination block.
//
// This method does this because all of the PHI nodes must be executed
// atomically, reading their inputs before any of the results are updated.  Not
// doing this can cause problems if the PHI nodes depend on other PHI nodes for
// their inputs.  If the input PHI node is updated before it is read, incorrect
// results can happen.  Thus we use a two phase approach.
//

void Interpreter::SwitchToNewBasicBlock(BasicBlock *Dest,
                                        ExecutionContext &SF) {
  BasicBlock *PrevBB = SF.CurBB;  // Remember where we came from...
  SF.CurBB = Dest;                // Update CurBB to branch destination
  SF.CurInst = SF.CurBB->begin(); // Update new instruction ptr...

  if (!isa<PHINode>(SF.CurInst))
    return; // Nothing fancy to do
  // Loop over all of the PHI nodes in the current block, reading their inputs.
  std::vector<GenericValue> ResultValues;

  for (; PHINode *PN = dyn_cast<PHINode>(SF.CurInst); ++SF.CurInst) {
    // Search for the value corresponding to this previous bb...
    int i = PN->getBasicBlockIndex(PrevBB);
    assert(i != -1 && "PHINode doesn't contain entry for predecessor??");
    Value *IncomingValue = PN->getIncomingValue(i);

    // Save the incoming value for this PHI node...
    ResultValues.push_back(getOperandValue(IncomingValue, SF));
  }

  // Now loop over all of the PHI nodes setting their values...
  SF.CurInst = SF.CurBB->begin();
  for (unsigned i = 0; isa<PHINode>(SF.CurInst); ++SF.CurInst, ++i) {
    GenericValue ResultValue = ResultValues[i];
    PHINode *PN = cast<PHINode>(SF.CurInst);
    SetValue(PN, ResultValue, SF);
  }
}

//===----------------------------------------------------------------------===//
//                     Memory Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::visitAllocaInst(AllocaInst &I) {
  ExecutionContext &SF = Interpreter::context();

  Type *Ty = I.getAllocatedType(); // Type to be allocated

  // Get the number of elements being allocated by the array...
  unsigned NumElements =
      getOperandValue(I.getOperand(0), SF).IntVal.getZExtValue();

  unsigned TypeSize = (size_t)getDataLayout().getTypeAllocSize(Ty);

  // Avoid malloc-ing zero bytes, use max()...
  uint64_t MemToAlloc = std::max(1U, NumElements * TypeSize);
  uint64_t Alignment = I.getAlign().value();

  if (Interpreter::ExecutionEngine::miriIsInitialized()) {
    MiriPointer MiriPointerVal = Interpreter::ExecutionEngine::MiriMalloc(
        Interpreter::ExecutionEngine::MiriWrapper, MemToAlloc, Alignment,
        false);
    LLVM_DEBUG(dbgs() << "Miri Allocated Type: " << *Ty << " (" << TypeSize
                      << " bytes) x " << NumElements
                      << " (Total: " << MemToAlloc << ") at "
                      << uintptr_t(MiriPointerVal.addr) << '\n');
    assert(MiriPointerVal.addr != 0 && "Null pointer returned by MiriMalloc!");
    GenericValue Result = MiriPointerTOGV(MiriPointerVal);
    SetValue(&I, Result, SF);
    if (I.getOpcode() == Instruction::Alloca)
      Interpreter::context().MiriAllocas.add(MiriPointerVal);
  } else {
    report_fatal_error("Miri isn't initialized.");
  }
}

// getElementOffset - The workhorse for getelementptr.
//
GenericValue Interpreter::executeGEPOperation(Value *Ptr, gep_type_iterator I,
                                              gep_type_iterator E,
                                              ExecutionContext &SF) {
  assert(Ptr->getType()->isPointerTy() &&
         "Cannot getElementOffset of a nonpointer type!");

  uint64_t Total = 0;

  for (; I != E; ++I) {
    if (StructType *STy = I.getStructTypeOrNull()) {
      const StructLayout *SLO = getDataLayout().getStructLayout(STy);

      const ConstantInt *CPU = cast<ConstantInt>(I.getOperand());
      unsigned Index = unsigned(CPU->getZExtValue());

      Total += SLO->getElementOffset(Index);
    } else {
      // Get the index number for the array... which must be long type...
      GenericValue IdxGV = getOperandValue(I.getOperand(), SF);

      int64_t Idx;
      unsigned BitWidth =
          cast<IntegerType>(I.getOperand()->getType())->getBitWidth();
      if (BitWidth == 32)
        Idx = (int64_t)(int32_t)IdxGV.IntVal.getZExtValue();
      else {
        assert(BitWidth == 64 && "Invalid index type for getelementptr");
        Idx = (int64_t)IdxGV.IntVal.getZExtValue();
      }
      Total += getDataLayout().getTypeAllocSize(I.getIndexedType()) * Idx;
    }
  }

  GenericValue Result;
  GenericValue OperandValue = getOperandValue(Ptr, SF);
  if (ExecutionEngine::miriIsInitialized()) {
    MiriPointer OperandMiriPointerVal = GVTOMiriPointer(OperandValue);
    Result = MiriPointerTOGV(ExecutionEngine::MiriGetElementPointer(
        ExecutionEngine::MiriWrapper, OperandMiriPointerVal, Total));
  } else {
    Result.PointerVal = ((char *)OperandValue.PointerVal) + Total;
    Result.Provenance = OperandValue.Provenance;
  }
  LLVM_DEBUG(dbgs() << "GEP Index " << Total << " bytes.\n");
  return Result;
}

void Interpreter::visitGetElementPtrInst(GetElementPtrInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I,
           executeGEPOperation(I.getPointerOperand(), gep_type_begin(I),
                               gep_type_end(I), SF),
           SF);
}

void Interpreter::visitLoadInst(LoadInst &I) {
  ExecutionContext &SF = Interpreter::context();
  GenericValue SRC = getOperandValue(I.getPointerOperand(), SF);
  GenericValue Result;
  MiriPointer MiriPointerVal = GVTOMiriPointer(SRC);
  if (ExecutionEngine::miriIsInitialized()) {
    LLVM_DEBUG(dbgs() << "Loading value from Miri memory, address: "
                      << MiriPointerVal.addr << " ");
    Type *LoadType = I.getType();
    if (auto *TETy = dyn_cast<TargetExtType>(LoadType))
      LoadType = TETy->getLayoutType();
    const unsigned LoadBytes = getDataLayout().getTypeStoreSize(LoadType);
    uint64_t LoadAlign = getDataLayout().getABITypeAlign(LoadType).value();
    bool status = Interpreter::ExecutionEngine::LoadFromMiriMemory(
        &Result, MiriPointerVal, LoadType, LoadBytes, LoadAlign);
    if (status) {
      Interpreter::registerMiriError(I);
      return;
    }
  } else {
    report_fatal_error("Miri isn't initialized.");
  }
  SetValue(&I, Result, SF);
  if (I.isVolatile() && PrintVolatile)
    dbgs() << "Volatile load " << I;
}

void Interpreter::visitStoreInst(StoreInst &I) {
  ExecutionContext &SF = Interpreter::context();
  GenericValue Val = getOperandValue(I.getOperand(0), SF);
  GenericValue SRC = getOperandValue(I.getPointerOperand(), SF);

  MiriPointer MiriPointerVal = GVTOMiriPointer(SRC);

  if (ExecutionEngine::miriIsInitialized()) {
    LLVM_DEBUG(dbgs() << "Storing value to Miri memory, address: "
                      << MiriPointerVal.addr << " ");
    Type *StoreType = I.getOperand(0)->getType();
    if (auto *TETy = dyn_cast<TargetExtType>(StoreType))
      StoreType = TETy->getLayoutType();
    const unsigned StoreBytes = getDataLayout().getTypeStoreSize(StoreType);
    uint64_t StoreAlign = getDataLayout().getABITypeAlign(StoreType).value();

    bool status = Interpreter::ExecutionEngine::StoreToMiriMemory(
        &Val, MiriPointerVal, StoreType, StoreBytes, StoreAlign);
    if (status) {
      Interpreter::registerMiriError(I);
      return;
    }
  } else {
    report_fatal_error("Miri isn't initialized.");
  }
  if (I.isVolatile() && PrintVolatile)
    dbgs() << "Volatile store: " << I;
}

//===----------------------------------------------------------------------===//
//                 Miscellaneous Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::visitVAStartInst(VAStartInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Value *DestinationOperand = I.getOperand(0);
  GenericValue Destination = getOperandValue(DestinationOperand, SF);
  if (ExecutionEngine::miriIsInitialized()) {
    MiriPointer MiriPointerVal = GVTOMiriPointer(Destination);
    GenericValue ArgIndex;
    ArgIndex.UIntPairVal.first = Interpreter::stackSize() - 1;
    ArgIndex.UIntPairVal.second = 0;
    // there are two possible options for how a va_list is represented
    // for most systems, it's a pointer. for Unix x86_64, it's a
    // struct containing two 32-bit integers and two pointers. {u32, u32, ptr,
    // ptr} either way, we can guarantee that there's enough memory for a 64-bit
    // word, which is the same width as the pointer argument to va_start.
    Type *StoreType = DestinationOperand->getType();
    if (auto *TETy = dyn_cast<TargetExtType>(StoreType))
      StoreType = TETy->getLayoutType();

    const unsigned StoreBytes = getDataLayout().getTypeStoreSize(StoreType);
    uint64_t StoreAlign = getDataLayout().getABITypeAlign(StoreType).value();
    bool status = Interpreter::ExecutionEngine::StoreToMiriMemory(
        &ArgIndex, MiriPointerVal, StoreType, StoreBytes, StoreAlign);
    if (status) {
      Interpreter::registerMiriError(I);
      return;
    }
  } else {
    report_fatal_error("Miri isn't initialized.");
  }
}

void Interpreter::visitVAEndInst(VAEndInst &I) {
  // va_end is a noop for the interpreter
}

void Interpreter::visitVACopyInst(VACopyInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Value *DestValue = I.getOperand(0);
  Value *SourceValue = I.getOperand(1);

  GenericValue Dest = getOperandValue(DestValue, SF);
  GenericValue Src = getOperandValue(SourceValue, SF);
  if (ExecutionEngine::miriIsInitialized()) {
    MiriPointer DestMiriPointerVal = GVTOMiriPointer(Dest);
    MiriPointer SrcMiriPointerVal = GVTOMiriPointer(Src);
    // there are two possible options for how a va_list is represented
    // for most systems, it's a pointer. for Unix x86_64, it's a
    // struct containing two 32-bit integers and two pointers. {u32, u32, ptr,
    // ptr} either way, we can guarantee that there's enough memory for a 64-bit
    // word, which is the same width as the pointer argument to va_start.
    Type *OpaquePointerType = DestValue->getType();
    if (auto *TETy = dyn_cast<TargetExtType>(OpaquePointerType))
      OpaquePointerType = TETy->getLayoutType();

    const unsigned OpaquePointerBytes =
        getDataLayout().getTypeStoreSize(OpaquePointerType);
    uint64_t OpaquePointerAlign =
        getDataLayout().getABITypeAlign(OpaquePointerType).value();
    GenericValue SourceArgIndex;

    bool LoadStatus = Interpreter::ExecutionEngine::LoadFromMiriMemory(
        &SourceArgIndex, SrcMiriPointerVal, OpaquePointerType,
        OpaquePointerBytes, OpaquePointerAlign);

    if (LoadStatus) {
      Interpreter::registerMiriError(I);
      return;
    }
    bool StoreStatus = Interpreter::ExecutionEngine::StoreToMiriMemory(
        &SourceArgIndex, DestMiriPointerVal, OpaquePointerType,
        OpaquePointerBytes, OpaquePointerAlign);
    if (StoreStatus) {
      Interpreter::registerMiriError(I);
      return;
    }
  } else {
    report_fatal_error("Miri isn't initialized.");
  }
}

static GenericValue executeIntrinsicFabsInst(GenericValue Src1, Type *Ty) {
  GenericValue Dest;

  switch (Ty->getTypeID()) {
  case Type::FloatTyID: {
    Dest.FloatVal = fabsf(Src1.FloatVal);
  } break;
  case Type::DoubleTyID: {
    Dest.DoubleVal = fabs(Src1.DoubleVal);
  } break;
  case Type::IntegerTyID:
    Dest.IntVal = Src1.IntVal.abs();
    break;
  default:
    report_fatal_error("fabs intrinsic only supports float, double, or int");
  }
  return Dest;
}

GenericValue executeIntrinsicFmuladdInst(GenericValue Src1, GenericValue Src2,
                                         GenericValue Src3, Type *Ty) {
  GenericValue Dest;
  switch (Ty->getTypeID()) {
  case Type::FloatTyID: {
    float FSrc1 = Src1.FloatVal;
    float FSrc2 = Src2.FloatVal;
    float FSrc3 = Src3.FloatVal;
    Dest.FloatVal = std::fmaf(FSrc1, FSrc2, FSrc3);
  } break;
  case Type::DoubleTyID: {
    double FSrc1 = Src1.DoubleVal;
    double FSrc2 = Src2.DoubleVal;
    double FSrc3 = Src3.DoubleVal;
    Dest.DoubleVal = std::fma(FSrc1, FSrc2, FSrc3);
  } break;
  default: {
    report_fatal_error("fmuladd intrinsic only supports float and double");
  }
  }
  return Dest;
}

GenericValue executeIntrinsicFshIntInst(GenericValue Src1, GenericValue Src2,
                                                GenericValue Src3, bool isLeft)
{ GenericValue Dest;

  assert(Src1.IntVal.getBitWidth() == Src2.IntVal.getBitWidth());
  assert(Src2.IntVal.getBitWidth() == Src3.IntVal.getBitWidth());

  unsigned bitWidth = Src1.IntVal.getBitWidth();
  APInt concat = Src1.IntVal <<=  bitWidth | Src2.IntVal;

  if (isLeft) {
    Dest.IntVal = concat.rotl(Src3.IntVal);
  } else {
    Dest.IntVal = concat.rotr(Src3.IntVal);
  }

  return Dest;
}

static GenericValue executeIntrinsicFshInst(GenericValue Src1, GenericValue
Src2, GenericValue Src3, Type* Ty, bool isLeft) {

  GenericValue Dest;

  // the operands are vectors
  if (Ty->isVectorTy()) {
    report_fatal_error("funnel shift intrinsics do not support vectors yet.");
  } else {
    assert(Ty->isIntegerTy());
    Dest = executeIntrinsicFshIntInst(Src1, Src2, Src3, isLeft);
  }
  return Dest;
}

void Interpreter::visitIntrinsicInst(IntrinsicInst &I) {
  ExecutionContext &SF = Interpreter::context();

  // If it is an unknown intrinsic function, use the intrinsic lowering
  // class to transform it into hopefully tasty LLVM code.
  //

  switch (I.getIntrinsicID()) {
  case Intrinsic::objectsize: {
    SetValue(&I,
             getOperandValue(
                 lowerObjectSizeCall(&I, getDataLayout(), nullptr, true), SF),
             SF);
    return;
  }

  case Intrinsic::is_constant: {
    Value *Flag = ConstantInt::getFalse(I.getType());
    if (auto *C = dyn_cast<Constant>(I.getOperand(0)))
      if (C->isManifestConstant())
        Flag = ConstantInt::getTrue(I.getType());
    SetValue(&I, getOperandValue(Flag, SF), SF);
    return;
  }

  case Intrinsic::fmuladd: {
    Type *Ty1 = I.getOperand(0)->getType();
    Type *Ty2 = I.getOperand(1)->getType();
    Type *Ty3 = I.getOperand(2)->getType();

    assert(Ty1->getTypeID() == Ty2->getTypeID());
    assert(Ty2->getTypeID() == Ty3->getTypeID());

    GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
    GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
    GenericValue Src3 = getOperandValue(I.getOperand(2), SF);

    GenericValue R = executeIntrinsicFmuladdInst(Src1, Src2, Src3, Ty1);
    SetValue(&I, R, SF);
    return;
  }

  case Intrinsic::fshl: {
    Type *Ty1 = I.getOperand(0)->getType();
    Type *Ty2 = I.getOperand(1)->getType();
    Type *Ty3 = I.getOperand(2)->getType();

    assert(Ty1->getTypeID() == Ty2->getTypeID());
    assert(Ty2->getTypeID() == Ty3->getTypeID());

    GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
    GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
    GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
    GenericValue R = executeIntrinsicFshInst(Src1, Src2, Src3, Ty1, true);
    SetValue(&I, R, SF);
    ++SF.CurInst;
    return;
  }

  case Intrinsic::fshr: {

    Type *Ty1 = I.getOperand(0)->getType();
    Type *Ty2 = I.getOperand(1)->getType();
    Type *Ty3 = I.getOperand(2)->getType();

    assert(Ty1->getTypeID() == Ty2->getTypeID());
    assert(Ty2->getTypeID() == Ty3->getTypeID());

    GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
    GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
    GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
    GenericValue R = executeIntrinsicFshInst(Src1, Src2, Src3, Ty1, false);
    SetValue(&I, R, SF);
    ++SF.CurInst;
    return;
  }

  case Intrinsic::fabs: {
    Type *Ty = I.getOperand(0)->getType();
    GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
    GenericValue R = executeIntrinsicFabsInst(Src1, Ty);
    SetValue(&I, R, SF);
    return;
  }
  default: {
    BasicBlock::iterator Me(&I);
    BasicBlock *Parent = I.getParent();
    bool atBegin(Parent->begin() == Me);
    if (!atBegin)
      --Me;
    IL->LowerIntrinsicCall(&I);
    // Restore the CurInst pointer to the first instruction newly inserted, if
    // any.
    if (atBegin) {
      SF.CurInst = Parent->begin();
    } else {
      SF.CurInst = Me;
      ++SF.CurInst;
    }
  } break;
  }
}

void Interpreter::visitCallBase(CallBase &I) {
  if (I.isInlineAsm()) {
    std::string Message = "Inline assembly instruction not supported: " +
                          std::string(I.getName());
    report_fatal_error(Message.data());
  }
  ExecutionContext &SF = Interpreter::context();
  SF.Caller = &I;
  std::vector<GenericValue> ArgVals;
  const unsigned NumArgs = SF.Caller->arg_size();
  ArgVals.reserve(NumArgs);
  for (Value *V : SF.Caller->args())
    ArgVals.push_back(getOperandValue(V, SF));
  // To handle indirect calls, we must get the pointer value from the argument
  // and treat it as a function pointer.
  GenericValue SRC = getOperandValue(SF.Caller->getCalledOperand(), SF);
  if (SRC.Provenance.alloc_id != 0) {
    Interpreter::CallMiriFunctionByPointer(I.getFunctionType(), SRC, ArgVals);
    SF.MustResolvePendingReturn = true;
    return;
  } else {
    callFunction((Function *)GVTOP(SRC), ArgVals);
  }
}

// auxiliary function for shift operations
static unsigned getShiftAmount(uint64_t orgShiftAmount,
                               llvm::APInt valueToShift) {
  unsigned valueWidth = valueToShift.getBitWidth();
  if (orgShiftAmount < (uint64_t)valueWidth)
    return orgShiftAmount;
  // according to the llvm documentation, if orgShiftAmount > valueWidth,
  // the result is undfeined. but we do shift by this rule:
  return (NextPowerOf2(valueWidth - 1) - 1) & orgShiftAmount;
}

void Interpreter::visitShl(BinaryOperator &I) {
  ExecutionContext &SF = Interpreter::context();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Dest;
  Type *Ty = I.getType();

  if (Ty->isVectorTy()) {
    uint32_t src1Size = uint32_t(Src1.AggregateVal.size());
    assert(src1Size == Src2.AggregateVal.size());
    for (unsigned i = 0; i < src1Size; i++) {
      GenericValue Result;
      uint64_t shiftAmount = Src2.AggregateVal[i].IntVal.getZExtValue();
      llvm::APInt valueToShift = Src1.AggregateVal[i].IntVal;
      Result.IntVal =
          valueToShift.shl(getShiftAmount(shiftAmount, valueToShift));
      Dest.AggregateVal.push_back(Result);
    }
  } else {
    // scalar
    uint64_t shiftAmount = Src2.IntVal.getZExtValue();
    llvm::APInt valueToShift = Src1.IntVal;
    Dest.IntVal = valueToShift.shl(getShiftAmount(shiftAmount, valueToShift));
  }

  SetValue(&I, Dest, SF);
}

void Interpreter::visitLShr(BinaryOperator &I) {
  ExecutionContext &SF = Interpreter::context();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Dest;
  Type *Ty = I.getType();

  if (Ty->isVectorTy()) {
    uint32_t src1Size = uint32_t(Src1.AggregateVal.size());
    assert(src1Size == Src2.AggregateVal.size());
    for (unsigned i = 0; i < src1Size; i++) {
      GenericValue Result;
      uint64_t shiftAmount = Src2.AggregateVal[i].IntVal.getZExtValue();
      llvm::APInt valueToShift = Src1.AggregateVal[i].IntVal;
      Result.IntVal =
          valueToShift.lshr(getShiftAmount(shiftAmount, valueToShift));
      Dest.AggregateVal.push_back(Result);
    }
  } else {
    // scalar
    uint64_t shiftAmount = Src2.IntVal.getZExtValue();
    llvm::APInt valueToShift = Src1.IntVal;
    Dest.IntVal = valueToShift.lshr(getShiftAmount(shiftAmount, valueToShift));
  }

  SetValue(&I, Dest, SF);
}

void Interpreter::visitAShr(BinaryOperator &I) {
  ExecutionContext &SF = Interpreter::context();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Dest;
  Type *Ty = I.getType();

  if (Ty->isVectorTy()) {
    size_t src1Size = Src1.AggregateVal.size();
    assert(src1Size == Src2.AggregateVal.size());
    for (unsigned i = 0; i < src1Size; i++) {
      GenericValue Result;
      uint64_t shiftAmount = Src2.AggregateVal[i].IntVal.getZExtValue();
      llvm::APInt valueToShift = Src1.AggregateVal[i].IntVal;
      Result.IntVal =
          valueToShift.ashr(getShiftAmount(shiftAmount, valueToShift));
      Dest.AggregateVal.push_back(Result);
    }
  } else {
    // scalar
    uint64_t shiftAmount = Src2.IntVal.getZExtValue();
    llvm::APInt valueToShift = Src1.IntVal;
    Dest.IntVal = valueToShift.ashr(getShiftAmount(shiftAmount, valueToShift));
  }

  SetValue(&I, Dest, SF);
}

GenericValue Interpreter::executeTruncInst(Value *SrcVal, Type *DstTy,
                                           ExecutionContext &SF) {
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);
  Type *SrcTy = SrcVal->getType();
  if (SrcTy->isVectorTy()) {
    Type *DstVecTy = DstTy->getScalarType();
    unsigned DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
    unsigned NumElts = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal
    Dest.AggregateVal.resize(NumElts);
    for (unsigned i = 0; i < NumElts; i++)
      Dest.AggregateVal[i].IntVal = Src.AggregateVal[i].IntVal.trunc(DBitWidth);
  } else {
    IntegerType *DITy = cast<IntegerType>(DstTy);
    unsigned DBitWidth = DITy->getBitWidth();
    Dest.IntVal = Src.IntVal.trunc(DBitWidth);
  }
  return Dest;
}

GenericValue Interpreter::executeSExtInst(Value *SrcVal, Type *DstTy,
                                          ExecutionContext &SF) {
  Type *SrcTy = SrcVal->getType();
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);
  if (SrcTy->isVectorTy()) {
    Type *DstVecTy = DstTy->getScalarType();
    unsigned DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal.
    Dest.AggregateVal.resize(size);
    for (unsigned i = 0; i < size; i++)
      Dest.AggregateVal[i].IntVal = Src.AggregateVal[i].IntVal.sext(DBitWidth);
  } else {
    auto *DITy = cast<IntegerType>(DstTy);
    unsigned DBitWidth = DITy->getBitWidth();
    Dest.IntVal = Src.IntVal.sext(DBitWidth);
  }
  return Dest;
}

GenericValue Interpreter::executeZExtInst(Value *SrcVal, Type *DstTy,
                                          ExecutionContext &SF) {
  Type *SrcTy = SrcVal->getType();
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);
  if (SrcTy->isVectorTy()) {
    Type *DstVecTy = DstTy->getScalarType();
    unsigned DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();

    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal.
    Dest.AggregateVal.resize(size);
    for (unsigned i = 0; i < size; i++)
      Dest.AggregateVal[i].IntVal = Src.AggregateVal[i].IntVal.zext(DBitWidth);
  } else {
    auto *DITy = cast<IntegerType>(DstTy);
    unsigned DBitWidth = DITy->getBitWidth();
    Dest.IntVal = Src.IntVal.zext(DBitWidth);
  }
  return Dest;
}

GenericValue Interpreter::executeFPTruncInst(Value *SrcVal, Type *DstTy,
                                             ExecutionContext &SF) {
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcVal->getType())) {
    assert(SrcVal->getType()->getScalarType()->isDoubleTy() &&
           DstTy->getScalarType()->isFloatTy() &&
           "Invalid FPTrunc instruction");

    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal.
    Dest.AggregateVal.resize(size);
    for (unsigned i = 0; i < size; i++)
      Dest.AggregateVal[i].FloatVal = (float)Src.AggregateVal[i].DoubleVal;
  } else {
    assert(SrcVal->getType()->isDoubleTy() && DstTy->isFloatTy() &&
           "Invalid FPTrunc instruction");
    Dest.FloatVal = (float)Src.DoubleVal;
  }

  return Dest;
}

GenericValue Interpreter::executeFPExtInst(Value *SrcVal, Type *DstTy,
                                           ExecutionContext &SF) {
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcVal->getType())) {
    assert(SrcVal->getType()->getScalarType()->isFloatTy() &&
           DstTy->getScalarType()->isDoubleTy() && "Invalid FPExt instruction");

    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal.
    Dest.AggregateVal.resize(size);
    for (unsigned i = 0; i < size; i++)
      Dest.AggregateVal[i].DoubleVal = (double)Src.AggregateVal[i].FloatVal;
  } else {
    assert(SrcVal->getType()->isFloatTy() && DstTy->isDoubleTy() &&
           "Invalid FPExt instruction");
    Dest.DoubleVal = (double)Src.FloatVal;
  }

  return Dest;
}

GenericValue Interpreter::executeFPToUIInst(Value *SrcVal, Type *DstTy,
                                            ExecutionContext &SF) {
  Type *SrcTy = SrcVal->getType();
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcTy)) {
    Type *DstVecTy = DstTy->getScalarType();
    Type *SrcVecTy = SrcTy->getScalarType();
    uint32_t DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal.
    Dest.AggregateVal.resize(size);

    if (SrcVecTy->getTypeID() == Type::FloatTyID) {
      assert(SrcVecTy->isFloatingPointTy() && "Invalid FPToUI instruction");
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].IntVal = APIntOps::RoundFloatToAPInt(
            Src.AggregateVal[i].FloatVal, DBitWidth);
    } else {
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].IntVal = APIntOps::RoundDoubleToAPInt(
            Src.AggregateVal[i].DoubleVal, DBitWidth);
    }
  } else {
    // scalar
    uint32_t DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
    assert(SrcTy->isFloatingPointTy() && "Invalid FPToUI instruction");
    if (SrcTy->getTypeID() == Type::FloatTyID)
      Dest.IntVal = APIntOps::RoundFloatToAPInt(Src.FloatVal, DBitWidth);
    else {
      Dest.IntVal = APIntOps::RoundDoubleToAPInt(Src.DoubleVal, DBitWidth);
    }
  }

  return Dest;
}

GenericValue Interpreter::executeFPToSIInst(Value *SrcVal, Type *DstTy,
                                            ExecutionContext &SF) {
  Type *SrcTy = SrcVal->getType();
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcTy)) {
    Type *DstVecTy = DstTy->getScalarType();
    Type *SrcVecTy = SrcTy->getScalarType();
    uint32_t DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal
    Dest.AggregateVal.resize(size);

    if (SrcVecTy->getTypeID() == Type::FloatTyID) {
      assert(SrcVecTy->isFloatingPointTy() && "Invalid FPToSI instruction");
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].IntVal = APIntOps::RoundFloatToAPInt(
            Src.AggregateVal[i].FloatVal, DBitWidth);
    } else {
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].IntVal = APIntOps::RoundDoubleToAPInt(
            Src.AggregateVal[i].DoubleVal, DBitWidth);
    }
  } else {
    // scalar
    unsigned DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
    assert(SrcTy->isFloatingPointTy() && "Invalid FPToSI instruction");

    if (SrcTy->getTypeID() == Type::FloatTyID)
      Dest.IntVal = APIntOps::RoundFloatToAPInt(Src.FloatVal, DBitWidth);
    else {
      Dest.IntVal = APIntOps::RoundDoubleToAPInt(Src.DoubleVal, DBitWidth);
    }
  }
  return Dest;
}

GenericValue Interpreter::executeUIToFPInst(Value *SrcVal, Type *DstTy,
                                            ExecutionContext &SF) {
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcVal->getType())) {
    Type *DstVecTy = DstTy->getScalarType();
    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal
    Dest.AggregateVal.resize(size);

    if (DstVecTy->getTypeID() == Type::FloatTyID) {
      assert(DstVecTy->isFloatingPointTy() && "Invalid UIToFP instruction");
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].FloatVal =
            APIntOps::RoundAPIntToFloat(Src.AggregateVal[i].IntVal);
    } else {
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].DoubleVal =
            APIntOps::RoundAPIntToDouble(Src.AggregateVal[i].IntVal);
    }
  } else {
    // scalar
    assert(DstTy->isFloatingPointTy() && "Invalid UIToFP instruction");
    if (DstTy->getTypeID() == Type::FloatTyID)
      Dest.FloatVal = APIntOps::RoundAPIntToFloat(Src.IntVal);
    else {
      Dest.DoubleVal = APIntOps::RoundAPIntToDouble(Src.IntVal);
    }
  }
  return Dest;
}

GenericValue Interpreter::executeSIToFPInst(Value *SrcVal, Type *DstTy,
                                            ExecutionContext &SF) {
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcVal->getType())) {
    Type *DstVecTy = DstTy->getScalarType();
    unsigned size = Src.AggregateVal.size();
    // the sizes of src and dst vectors must be equal
    Dest.AggregateVal.resize(size);

    if (DstVecTy->getTypeID() == Type::FloatTyID) {
      assert(DstVecTy->isFloatingPointTy() && "Invalid SIToFP instruction");
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].FloatVal =
            APIntOps::RoundSignedAPIntToFloat(Src.AggregateVal[i].IntVal);
    } else {
      for (unsigned i = 0; i < size; i++)
        Dest.AggregateVal[i].DoubleVal =
            APIntOps::RoundSignedAPIntToDouble(Src.AggregateVal[i].IntVal);
    }
  } else {
    // scalar
    assert(DstTy->isFloatingPointTy() && "Invalid SIToFP instruction");

    if (DstTy->getTypeID() == Type::FloatTyID)
      Dest.FloatVal = APIntOps::RoundSignedAPIntToFloat(Src.IntVal);
    else {
      Dest.DoubleVal = APIntOps::RoundSignedAPIntToDouble(Src.IntVal);
    }
  }

  return Dest;
}

GenericValue Interpreter::executePtrToIntInst(Value *SrcVal, Type *DstTy,
                                              ExecutionContext &SF) {
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);
  assert(SrcVal->getType()->isPointerTy() && "Invalid PtrToInt instruction");
  if (ExecutionEngine::miriIsInitialized()) {
    uint64_t SrcAsInt = ExecutionEngine::MPtrToInt(ExecutionEngine::MiriWrapper,
                                                   GVTOMiriPointer(Src));
    Dest.IntVal = APInt(MIRI_POINTER_BIT_WIDTH, SrcAsInt);
    return Dest;
  } else {
    report_fatal_error("Miri is not initialized");
  }
}

GenericValue Interpreter::executeIntToPtrInst(Value *SrcVal, Type *DstTy,
                                              ExecutionContext &SF) {
  GenericValue Src = getOperandValue(SrcVal, SF);
  assert(DstTy->isPointerTy() && "Invalid PtrToInt instruction");
  if (MIRI_POINTER_BIT_WIDTH != Src.IntVal.getBitWidth())
    Src.IntVal = Src.IntVal.zextOrTrunc(MIRI_POINTER_BIT_WIDTH);
  if (ExecutionEngine::miriIsInitialized()) {
    MiriPointer Converted = ExecutionEngine::MIntToPtr(
        ExecutionEngine::MiriWrapper, uint64_t(Src.IntVal.getZExtValue()));
    return MiriPointerTOGV(Converted);
  } else {
    report_fatal_error("Miri is not initialized");
  }
}

GenericValue Interpreter::executeBitCastInst(Value *SrcVal, Type *DstTy,
                                             ExecutionContext &SF) {

  // This instruction supports bitwise conversion of vectors to integers and
  // to vectors of other types (as long as they have the same size)
  Type *SrcTy = SrcVal->getType();
  GenericValue Dest, Src = getOperandValue(SrcVal, SF);

  if (isa<VectorType>(SrcTy) || isa<VectorType>(DstTy)) {
    // vector src bitcast to vector dst or vector src bitcast to scalar dst or
    // scalar src bitcast to vector dst
    bool isLittleEndian = getDataLayout().isLittleEndian();
    GenericValue TempDst, TempSrc, SrcVec;
    TempDst.Provenance = Dest.Provenance;
    TempSrc.Provenance = Src.Provenance;

    Type *SrcElemTy;
    Type *DstElemTy;
    unsigned SrcBitSize;
    unsigned DstBitSize;
    unsigned SrcNum;
    unsigned DstNum;

    if (isa<VectorType>(SrcTy)) {
      SrcElemTy = SrcTy->getScalarType();
      SrcBitSize = SrcTy->getScalarSizeInBits();
      SrcNum = Src.AggregateVal.size();
      SrcVec = Src;
    } else {
      // if src is scalar value, make it vector <1 x type>
      SrcElemTy = SrcTy;
      SrcBitSize = SrcTy->getPrimitiveSizeInBits();
      SrcNum = 1;
      SrcVec.AggregateVal.push_back(Src);
    }

    if (isa<VectorType>(DstTy)) {
      DstElemTy = DstTy->getScalarType();
      DstBitSize = DstTy->getScalarSizeInBits();
      DstNum = (SrcNum * SrcBitSize) / DstBitSize;
    } else {
      DstElemTy = DstTy;
      DstBitSize = DstTy->getPrimitiveSizeInBits();
      DstNum = 1;
    }

    if (SrcNum * SrcBitSize != DstNum * DstBitSize)
      report_fatal_error("Invalid BitCast");

    // If src is floating point, cast to integer first.
    TempSrc.AggregateVal.resize(SrcNum);
    if (SrcElemTy->isFloatTy()) {
      for (unsigned i = 0; i < SrcNum; i++)
        TempSrc.AggregateVal[i].IntVal =
            APInt::floatToBits(SrcVec.AggregateVal[i].FloatVal);

    } else if (SrcElemTy->isDoubleTy()) {
      for (unsigned i = 0; i < SrcNum; i++)
        TempSrc.AggregateVal[i].IntVal =
            APInt::doubleToBits(SrcVec.AggregateVal[i].DoubleVal);
    } else if (SrcElemTy->isIntegerTy()) {
      for (unsigned i = 0; i < SrcNum; i++)
        TempSrc.AggregateVal[i].IntVal = SrcVec.AggregateVal[i].IntVal;
    } else {
      // Pointers are not allowed as the element type of vector.
      report_fatal_error("Invalid Bitcast");
    }

    // now TempSrc is integer type vector
    if (DstNum < SrcNum) {
      // Example: bitcast <4 x i32> <i32 0, i32 1, i32 2, i32 3> to <2 x i64>
      unsigned Ratio = SrcNum / DstNum;
      unsigned SrcElt = 0;
      for (unsigned i = 0; i < DstNum; i++) {
        GenericValue Elt;
        Elt.IntVal = 0;
        Elt.IntVal = Elt.IntVal.zext(DstBitSize);
        unsigned ShiftAmt = isLittleEndian ? 0 : SrcBitSize * (Ratio - 1);
        for (unsigned j = 0; j < Ratio; j++) {
          APInt Tmp;
          Tmp = Tmp.zext(SrcBitSize);
          Tmp = TempSrc.AggregateVal[SrcElt++].IntVal;
          Tmp = Tmp.zext(DstBitSize);
          Tmp <<= ShiftAmt;
          ShiftAmt += isLittleEndian ? SrcBitSize : -SrcBitSize;
          Elt.IntVal |= Tmp;
        }
        TempDst.AggregateVal.push_back(Elt);
      }
    } else {
      // Example: bitcast <2 x i64> <i64 0, i64 1> to <4 x i32>
      unsigned Ratio = DstNum / SrcNum;
      for (unsigned i = 0; i < SrcNum; i++) {
        unsigned ShiftAmt = isLittleEndian ? 0 : DstBitSize * (Ratio - 1);
        for (unsigned j = 0; j < Ratio; j++) {
          GenericValue Elt;
          Elt.IntVal = Elt.IntVal.zext(SrcBitSize);
          Elt.IntVal = TempSrc.AggregateVal[i].IntVal;
          Elt.IntVal.lshrInPlace(ShiftAmt);
          // it could be DstBitSize == SrcBitSize, so check it
          if (DstBitSize < SrcBitSize)
            Elt.IntVal = Elt.IntVal.trunc(DstBitSize);
          ShiftAmt += isLittleEndian ? DstBitSize : -DstBitSize;
          TempDst.AggregateVal.push_back(Elt);
        }
      }
    }

    // convert result from integer to specified type
    if (isa<VectorType>(DstTy)) {
      if (DstElemTy->isDoubleTy()) {
        Dest.AggregateVal.resize(DstNum);
        for (unsigned i = 0; i < DstNum; i++)
          Dest.AggregateVal[i].DoubleVal =
              TempDst.AggregateVal[i].IntVal.bitsToDouble();
      } else if (DstElemTy->isFloatTy()) {
        Dest.AggregateVal.resize(DstNum);
        for (unsigned i = 0; i < DstNum; i++)
          Dest.AggregateVal[i].FloatVal =
              TempDst.AggregateVal[i].IntVal.bitsToFloat();
      } else {
        Dest = TempDst;
      }
    } else {
      if (DstElemTy->isDoubleTy())
        Dest.DoubleVal = TempDst.AggregateVal[0].IntVal.bitsToDouble();
      else if (DstElemTy->isFloatTy()) {
        Dest.FloatVal = TempDst.AggregateVal[0].IntVal.bitsToFloat();
      } else {
        Dest.IntVal = TempDst.AggregateVal[0].IntVal;
      }
    }
  } else { //  if (isa<VectorType>(SrcTy)) || isa<VectorType>(DstTy))

    // scalar src bitcast to scalar dst
    if (DstTy->isPointerTy()) {
      assert(SrcTy->isPointerTy() && "Invalid BitCast");
      Dest.PointerVal = Src.PointerVal;
      Dest.Provenance = Src.Provenance;
    } else if (DstTy->isIntegerTy()) {
      if (SrcTy->isFloatTy())
        Dest.IntVal = APInt::floatToBits(Src.FloatVal);
      else if (SrcTy->isDoubleTy()) {
        Dest.IntVal = APInt::doubleToBits(Src.DoubleVal);
      } else if (SrcTy->isIntegerTy()) {
        Dest.IntVal = Src.IntVal;
      } else {
        report_fatal_error("Invalid BitCast");
      }
    } else if (DstTy->isFloatTy()) {
      if (SrcTy->isIntegerTy())
        Dest.FloatVal = Src.IntVal.bitsToFloat();
      else {
        Dest.FloatVal = Src.FloatVal;
      }
    } else if (DstTy->isDoubleTy()) {
      if (SrcTy->isIntegerTy())
        Dest.DoubleVal = Src.IntVal.bitsToDouble();
      else {
        Dest.DoubleVal = Src.DoubleVal;
      }
    } else {
      report_fatal_error("Invalid Bitcast");
    }
  }

  return Dest;
}

void Interpreter::visitTruncInst(TruncInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeTruncInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitSExtInst(SExtInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeSExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitZExtInst(ZExtInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeZExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPTruncInst(FPTruncInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeFPTruncInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPExtInst(FPExtInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeFPExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitUIToFPInst(UIToFPInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeUIToFPInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitSIToFPInst(SIToFPInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeSIToFPInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPToUIInst(FPToUIInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeFPToUIInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitFPToSIInst(FPToSIInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeFPToSIInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitPtrToIntInst(PtrToIntInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executePtrToIntInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitIntToPtrInst(IntToPtrInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeIntToPtrInst(I.getOperand(0), I.getType(), SF), SF);
}

void Interpreter::visitBitCastInst(BitCastInst &I) {
  ExecutionContext &SF = Interpreter::context();
  SetValue(&I, executeBitCastInst(I.getOperand(0), I.getType(), SF), SF);
}

#define IMPLEMENT_VAARG(TY)                                                    \
  case Type::TY##TyID:                                                         \
    Dest.TY##Val = Src.TY##Val;                                                \
    break

void Interpreter::visitVAArgInst(VAArgInst &I) {
  ExecutionContext &SF = Interpreter::context();

  Value *VAOperand = I.getOperand(0);
  GenericValue VASrc = getOperandValue(VAOperand, SF);
  GenericValue Dest;

  if (ExecutionEngine::miriIsInitialized()) {
    MiriPointer VASrcMiriPointerVal = GVTOMiriPointer(VASrc);

    Type *OpaquePointerType = VAOperand->getType();
    if (auto *TETy = dyn_cast<TargetExtType>(OpaquePointerType))
      OpaquePointerType = TETy->getLayoutType();

    const unsigned OpaquePointerBytes =
        getDataLayout().getTypeStoreSize(OpaquePointerType);
    uint64_t OpaquePointerAlign =
        getDataLayout().getABITypeAlign(OpaquePointerType).value();

    GenericValue SourceArgIndex;
    bool LoadStatus = Interpreter::ExecutionEngine::LoadFromMiriMemory(
        &SourceArgIndex, VASrcMiriPointerVal, OpaquePointerType,
        OpaquePointerBytes, OpaquePointerAlign);
    if (LoadStatus) {
      Interpreter::registerMiriError(I);
      return;
    }

    uint64_t CurrentStackSize = Interpreter::currentStack().size();
    if (SourceArgIndex.UIntPairVal.first >= CurrentStackSize) {
      std::string Message = "Invalid va_list stack index " +
                            std::to_string(SourceArgIndex.UIntPairVal.first) +
                            " for stack size " +
                            std::to_string(CurrentStackSize);
      report_fatal_error(Message.c_str());
    }

    uint64_t CurrentVAArgListSize =
        Interpreter::currentStack()[SourceArgIndex.UIntPairVal.first]
            .VarArgs.size();
    if (SourceArgIndex.UIntPairVal.second >= CurrentVAArgListSize) {
      std::string Message = "Invalid va_list argument index " +
                            std::to_string(SourceArgIndex.UIntPairVal.second) +
                            " for argument list of size " +
                            std::to_string(CurrentVAArgListSize);
      report_fatal_error(Message.c_str());
    }

    GenericValue Src =
        Interpreter::currentStack()[SourceArgIndex.UIntPairVal.first]
            .VarArgs[SourceArgIndex.UIntPairVal.second];

    Type *Ty = I.getType();
    switch (Ty->getTypeID()) {
    case Type::IntegerTyID:
      Dest.IntVal = Src.IntVal;
      break;
    case Type::PointerTyID:
      Dest.PointerVal = Src.PointerVal;
      Dest.Provenance = Src.Provenance;
      break;
      IMPLEMENT_VAARG(Float);
      IMPLEMENT_VAARG(Double);
    default:
      std::string Message =
          "Unhandled type for vaarg instruction: " + type_to_string(Ty);
      report_fatal_error(Message.c_str());
    }

    // Set the Value of this Instruction.
    SetValue(&I, Dest, SF);

    // Move the pointer to the next vararg.
    ++SourceArgIndex.UIntPairVal.second;

    bool StoreStatus = Interpreter::ExecutionEngine::StoreToMiriMemory(
        &SourceArgIndex, VASrcMiriPointerVal, OpaquePointerType,
        OpaquePointerBytes, OpaquePointerAlign);
    if (StoreStatus) {
      Interpreter::registerMiriError(I);
      return;
    }
  } else {
    report_fatal_error("Miri isn't initialized.");
  }
}

void Interpreter::visitExtractElementInst(ExtractElementInst &I) {
  ExecutionContext &SF = Interpreter::context();
  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Dest;

  Type *Ty = I.getType();
  const unsigned indx = unsigned(Src2.IntVal.getZExtValue());

  MiriProvenance ElemProvenance = Src1.AggregateVal[indx].Provenance;
  Dest.Provenance = ElemProvenance;

  if (Src1.AggregateVal.size() > indx) {
    switch (Ty->getTypeID()) {
    default: {
      std::string Message = "Unhandled type for extractelement instruction: " +
                            type_to_string(Ty);
      report_fatal_error(Message.c_str());
    } break;
    case Type::IntegerTyID:
      Dest.IntVal = Src1.AggregateVal[indx].IntVal;
      break;
    case Type::FloatTyID:
      Dest.FloatVal = Src1.AggregateVal[indx].FloatVal;
      break;
    case Type::DoubleTyID:
      Dest.DoubleVal = Src1.AggregateVal[indx].DoubleVal;
      break;
    }
  } else {
    report_fatal_error("Invalid index in extractelement instruction\n");
  }
  SetValue(&I, Dest, SF);
}

void Interpreter::visitInsertElementInst(InsertElementInst &I) {
  ExecutionContext &SF = Interpreter::context();
  VectorType *Ty = cast<VectorType>(I.getType());

  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
  GenericValue Dest;

  Type *TyContained = Ty->getElementType();

  const unsigned indx = unsigned(Src3.IntVal.getZExtValue());
  Dest.AggregateVal = Src1.AggregateVal;

  if (Src1.AggregateVal.size() <= indx)
    report_fatal_error("Invalid index in insertelement instruction");
  switch (TyContained->getTypeID()) {
  default:
    report_fatal_error("Unhandled dest type for insertelement instruction");
  case Type::IntegerTyID:
    Dest.AggregateVal[indx].IntVal = Src2.IntVal;
    break;
  case Type::FloatTyID:
    Dest.AggregateVal[indx].FloatVal = Src2.FloatVal;
    break;
  case Type::DoubleTyID:
    Dest.AggregateVal[indx].DoubleVal = Src2.DoubleVal;
    break;
  }
  SetValue(&I, Dest, SF);
}

void Interpreter::visitShuffleVectorInst(ShuffleVectorInst &I) {
  ExecutionContext &SF = Interpreter::context();

  VectorType *Ty = cast<VectorType>(I.getType());

  GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Dest;

  // There is no need to check types of src1 and src2, because the compiled
  // bytecode can't contain different types for src1 and src2 for a
  // shufflevector instruction.

  Type *TyContained = Ty->getElementType();
  unsigned src1Size = (unsigned)Src1.AggregateVal.size();
  unsigned src2Size = (unsigned)Src2.AggregateVal.size();
  unsigned src3Size = I.getShuffleMask().size();

  Dest.AggregateVal.resize(src3Size);

  switch (TyContained->getTypeID()) {
  default:
    report_fatal_error("Unhandled dest type for insertelement instruction");
    break;
  case Type::IntegerTyID:
    for (unsigned i = 0; i < src3Size; i++) {
      unsigned j = std::max(0, I.getMaskValue(i));
      if (j < src1Size)
        Dest.AggregateVal[i].IntVal = Src1.AggregateVal[j].IntVal;
      else if (j < src1Size + src2Size)
        Dest.AggregateVal[i].IntVal = Src2.AggregateVal[j - src1Size].IntVal;
      else
        // The selector may not be greater than sum of lengths of first and
        // second operands and llasm should not allow situation like
        // %tmp = shufflevector <2 x i32> <i32 3, i32 4>, <2 x i32> undef,
        //                      <2 x i32> < i32 0, i32 5 >,
        // where i32 5 is invalid, but let it be additional check here:
        report_fatal_error("Invalid mask in shufflevector instruction");
    }
    break;
  case Type::FloatTyID:
    for (unsigned i = 0; i < src3Size; i++) {
      unsigned j = std::max(0, I.getMaskValue(i));
      if (j < src1Size)
        Dest.AggregateVal[i].FloatVal = Src1.AggregateVal[j].FloatVal;
      else if (j < src1Size + src2Size)
        Dest.AggregateVal[i].FloatVal =
            Src2.AggregateVal[j - src1Size].FloatVal;
      else
        report_fatal_error("Invalid mask in shufflevector instruction");
    }
    break;
  case Type::DoubleTyID:
    for (unsigned i = 0; i < src3Size; i++) {
      unsigned j = std::max(0, I.getMaskValue(i));
      if (j < src1Size)
        Dest.AggregateVal[i].DoubleVal = Src1.AggregateVal[j].DoubleVal;
      else if (j < src1Size + src2Size)
        Dest.AggregateVal[i].DoubleVal =
            Src2.AggregateVal[j - src1Size].DoubleVal;
      else
        report_fatal_error("Invalid mask in shufflevector instruction");
    }
    break;
  }
  SetValue(&I, Dest, SF);
}

void Interpreter::visitExtractValueInst(ExtractValueInst &I) {
  ExecutionContext &SF = Interpreter::context();
  Value *Agg = I.getAggregateOperand();
  GenericValue Dest;
  GenericValue Src = getOperandValue(Agg, SF);

  ExtractValueInst::idx_iterator IdxBegin = I.idx_begin();
  unsigned Num = I.getNumIndices();
  GenericValue *pSrc = &Src;

  for (unsigned i = 0; i < Num; ++i) {
    pSrc = &pSrc->AggregateVal[*IdxBegin];
    ++IdxBegin;
  }

  Type *IndexedType =
      ExtractValueInst::getIndexedType(Agg->getType(), I.getIndices());
  switch (IndexedType->getTypeID()) {
  default:
    report_fatal_error("Unhandled dest type for extractelement instruction");
    break;
  case Type::IntegerTyID:
    Dest.IntVal = pSrc->IntVal;
    break;
  case Type::FloatTyID:
    Dest.FloatVal = pSrc->FloatVal;
    break;
  case Type::DoubleTyID:
    Dest.DoubleVal = pSrc->DoubleVal;
    break;
  case Type::ArrayTyID:
  case Type::StructTyID:
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID:
    Dest.AggregateVal = pSrc->AggregateVal;
    break;
  case Type::PointerTyID: {
    Dest.PointerVal = pSrc->PointerVal;
    Dest.Provenance = pSrc->Provenance;
  } break;
  }
  SetValue(&I, Dest, SF);
}

void Interpreter::visitInsertValueInst(InsertValueInst &I) {

  ExecutionContext &SF = Interpreter::context();
  Value *Agg = I.getAggregateOperand();

  GenericValue Src1 = getOperandValue(Agg, SF);
  GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
  GenericValue Dest = Src1; // Dest is a slightly changed Src1

  ExtractValueInst::idx_iterator IdxBegin = I.idx_begin();
  unsigned Num = I.getNumIndices();

  GenericValue *pDest = &Dest;
  for (unsigned i = 0; i < Num; ++i) {
    pDest = &pDest->AggregateVal[*IdxBegin];
    ++IdxBegin;
  }
  // pDest points to the target value in the Dest now

  Type *IndexedType =
      ExtractValueInst::getIndexedType(Agg->getType(), I.getIndices());

  switch (IndexedType->getTypeID()) {
  default:
    report_fatal_error("Unhandled dest type for insertelement instruction");
    break;
  case Type::IntegerTyID:
    pDest->IntVal = Src2.IntVal;
    break;
  case Type::FloatTyID:
    pDest->FloatVal = Src2.FloatVal;
    break;
  case Type::DoubleTyID:
    pDest->DoubleVal = Src2.DoubleVal;
    break;
  case Type::ArrayTyID:
  case Type::StructTyID:
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID:
    pDest->AggregateVal = Src2.AggregateVal;
    break;
  case Type::PointerTyID: {
    pDest->PointerVal = Src2.PointerVal;
    pDest->Provenance = Src2.Provenance;
  } break;
  }
  SetValue(&I, Dest, SF);
}

GenericValue Interpreter::getConstantExprValue(ConstantExpr *CE,
                                               ExecutionContext &SF) {
  switch (CE->getOpcode()) {
  case Instruction::Trunc:
    return executeTruncInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::ZExt:
    return executeZExtInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::SExt:
    return executeSExtInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::FPTrunc:
    return executeFPTruncInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::FPExt:
    return executeFPExtInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::UIToFP:
    return executeUIToFPInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::SIToFP:
    return executeSIToFPInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::FPToUI:
    return executeFPToUIInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::FPToSI:
    return executeFPToSIInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::PtrToInt:
    return executePtrToIntInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::IntToPtr:
    return executeIntToPtrInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::BitCast:
    return executeBitCastInst(CE->getOperand(0), CE->getType(), SF);
  case Instruction::GetElementPtr:
    return executeGEPOperation(CE->getOperand(0), gep_type_begin(CE),
                               gep_type_end(CE), SF);
  case Instruction::FCmp:
  case Instruction::ICmp:
    return executeCmpInst(
        CE->getPredicate(), getOperandValue(CE->getOperand(0), SF),
        getOperandValue(CE->getOperand(1), SF), CE->getOperand(0)->getType());
  case Instruction::Select:
    return executeSelectInst(getOperandValue(CE->getOperand(0), SF),
                             getOperandValue(CE->getOperand(1), SF),
                             getOperandValue(CE->getOperand(2), SF),
                             CE->getOperand(0)->getType());
  default:
    break;
  }

  // The cases below here require a GenericValue parameter for the result
  // so we initialize one, compute it and then return it.
  GenericValue Op0 = getOperandValue(CE->getOperand(0), SF);
  GenericValue Op1 = getOperandValue(CE->getOperand(1), SF);
  GenericValue Dest;
  Type *Ty0 = CE->getOperand(0)->getType();
  switch (CE->getOpcode()) {
  case Instruction::Add:
    Dest.IntVal = Op0.IntVal + Op1.IntVal;
    break;
  case Instruction::Sub:
    Dest.IntVal = Op0.IntVal - Op1.IntVal;
    break;
  case Instruction::Mul:
    Dest.IntVal = Op0.IntVal * Op1.IntVal;
    break;
  case Instruction::FAdd:
    executeFAddInst(Dest, Op0, Op1, Ty0);
    break;
  case Instruction::FSub:
    executeFSubInst(Dest, Op0, Op1, Ty0);
    break;
  case Instruction::FMul:
    executeFMulInst(Dest, Op0, Op1, Ty0);
    break;
  case Instruction::FDiv:
    executeFDivInst(Dest, Op0, Op1, Ty0);
    break;
  case Instruction::FRem:
    executeFRemInst(Dest, Op0, Op1, Ty0);
    break;
  case Instruction::SDiv:
    Dest.IntVal = Op0.IntVal.sdiv(Op1.IntVal);
    break;
  case Instruction::UDiv:
    Dest.IntVal = Op0.IntVal.udiv(Op1.IntVal);
    break;
  case Instruction::URem:
    Dest.IntVal = Op0.IntVal.urem(Op1.IntVal);
    break;
  case Instruction::SRem:
    Dest.IntVal = Op0.IntVal.srem(Op1.IntVal);
    break;
  case Instruction::And:
    Dest.IntVal = Op0.IntVal & Op1.IntVal;
    break;
  case Instruction::Or:
    Dest.IntVal = Op0.IntVal | Op1.IntVal;
    break;
  case Instruction::Xor:
    Dest.IntVal = Op0.IntVal ^ Op1.IntVal;
    break;
  case Instruction::Shl:
    Dest.IntVal = Op0.IntVal.shl(Op1.IntVal.getZExtValue());
    break;
  case Instruction::LShr:
    Dest.IntVal = Op0.IntVal.lshr(Op1.IntVal.getZExtValue());
    break;
  case Instruction::AShr:
    Dest.IntVal = Op0.IntVal.ashr(Op1.IntVal.getZExtValue());
    break;
  default:
    dbgs() << "Unhandled ConstantExpr: " << *CE << "\n";
    report_fatal_error("Unhandled ConstantExpr");
  }
  return Dest;
}

GenericValue Interpreter::getOperandValue(Value *V, ExecutionContext &SF) {
  GenericValue OperandValue;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    OperandValue = getConstantExprValue(CE, SF);
  } else if (Constant *CPV = dyn_cast<Constant>(V)) {
    OperandValue = getConstantValue(CPV);
  } else if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    void *Addr = getPointerToGlobal(GV);
    MiriProvenance Prov = getProvenanceOfGlobalIfAvailable(Addr);
    MiriPointer Ptr = {(uint64_t)Addr, Prov};
    OperandValue = MiriPointerTOGV(Ptr);
  } else {
    OperandValue = SF.Values[V];
  }
  OperandValue.ValueTy = V->getType();
  return OperandValue;
}

//===----------------------------------------------------------------------===//
//                        Dispatch and Execution Code
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// callFunction - Execute the specified function...
//
void Interpreter::callFunction(Function *F, ArrayRef<GenericValue> ArgVals) {
  assert((Interpreter::stackIsEmpty() || !Interpreter::context().Caller ||
          Interpreter::context().Caller->arg_size() == ArgVals.size()) &&
         "Incorrect number of arguments passed into function call!");

  // Make a new stack frame... and fill it in.
  Interpreter::currentStack().emplace_back(
      Interpreter::ExecutionEngine::MiriWrapper,
      Interpreter::ExecutionEngine::MiriFree);
  ExecutionContext &StackFrame = Interpreter::context();
  StackFrame.CurFunction = F;

  // if(F->getFunctionType()->)
  // Special handling for external functions.
  if (F->isDeclaration()) {
    callExternalFunction(F, ArgVals);
    // Simulate a 'ret' instruction of the appropriate type.
    Interpreter::popContext();
    if (!Interpreter::stackIsEmpty())
      Interpreter::context().MustResolvePendingReturn = true;
    return;
  }
  // Get pointers to first LLVM BB & Instruction in function.
  StackFrame.CurBB = &F->front();
  StackFrame.CurInst = StackFrame.CurBB->begin();
  // Run through the function arguments and initialize their values...
  assert(
      (ArgVals.size() == F->arg_size() ||
       (ArgVals.size() > F->arg_size() && F->getFunctionType()->isVarArg())) &&
      "Invalid number of values passed to function invocation!");
  // Handle non-varargs arguments...
  unsigned i = 0;
  for (Function::arg_iterator AI = F->arg_begin(), E = F->arg_end(); AI != E;
       ++AI, ++i) {
    SetValue(&*AI, ArgVals[i], StackFrame);
  }
  // Handle varargs arguments...
  StackFrame.VarArgs.assign(ArgVals.begin() + i, ArgVals.end());
}

void Interpreter::run() {
  while (!Interpreter::stackIsEmpty()) {
    // Interpret a single instruction & increment the "PC".
    ExecutionContext &SF = Interpreter::context(); // Current stack frame
    Instruction &I = *SF.CurInst++;                // Increment before execute

    // Track the number of dynamic instructions executed.
    ++NumDynamicInsts;

    LLVM_DEBUG(dbgs() << "About to interpret: " << I << "\n");

    visit(I); // Dispatch to one of the visit* methods...
    if (ExecutionEngine::getMiriErrorFlag()) {
      // Error occurred, stop execution.
      break;
    }
  }
}
