//==- TypeVisitor.cpp ------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DPP_TYPEVISITOR_H
#define LLVM_ANALYSIS_DPP_TYPEVISITOR_H

#include "llvm/IR/Type.h"

namespace llvm {
namespace DPP {


template <typename SubClass>
struct TypeVisitor {
  bool visit(const Type *Ty) {
    switch(Ty->getTypeID()) {
    case Type::ArrayTyID:
      if (((SubClass *)this)->visitArrayType(
              static_cast<const ArrayType *>(Ty)))
        return visit(Ty->getArrayElementType());
      return false;
    case Type::HalfTyID:
    case Type::FloatTyID:
    case Type::DoubleTyID:
    case Type::X86_FP80TyID:
    case Type::FP128TyID:
    case Type::PPC_FP128TyID:
      return ((SubClass *)this)->visitFloatingPointType(Ty);
    case Type::FunctionTyID:
      return ((SubClass *)this)->visitFunctionType(
          static_cast<const FunctionType *>(Ty));
    case Type::IntegerTyID:
      return ((SubClass *)this)->visitIntegerType(
          static_cast<const IntegerType *>(Ty));
    case Type::PointerTyID:
      return ((SubClass *)this)->visitPointerType(
          static_cast<const PointerType *>(Ty));
    case Type::StructTyID:
      if (!((SubClass *)this)->visitStructType(
              static_cast<const StructType *>(Ty)))
        return false;
      for (auto I = 0U, E = Ty->getStructNumElements(); I < E; ++I)
          if (!visit(Ty->getStructElementType(I)))
            return false;
      return true;
    case Type::VectorTyID:
      if (((SubClass *)this)->visitVectorType(
              static_cast<const VectorType *>(Ty)))
        return visit(Ty->getVectorElementType());
      return false;
    case Type::VoidTyID:
      return ((SubClass *)this)->visitVoidType(Ty);
    default:
      return ((SubClass *)this)->visitOtherType(Ty);
    }
  }

  bool visitArrayType(const ArrayType *) { return true; }
  bool visitFloatingPointType(const Type *) { return true; }
  bool visitFunctionType(const FunctionType *) { return true; }
  bool visitIntegerType(const IntegerType *) { return true; }
  bool visitPointerType(const PointerType *) { return true; }
  bool visitStructType(const StructType *) { return true; }
  bool visitVectorType(const VectorType *) { return true; }
  bool visitVoidType(const Type *) { return true; }
  bool visitOtherType(const Type *) { return true; }
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_ANALYSIS_DPP_TYPEVISITOR_H
