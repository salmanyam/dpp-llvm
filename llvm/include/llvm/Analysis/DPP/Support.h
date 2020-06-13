//==- Support.cpp ----------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_H
#define LLVM_SUPPORT_H

namespace llvm {
namespace DPP {

// Enum -> String mapper from: https://stackoverflow.com/a/208003
template<typename T> struct map_init_helper {
  T& data;
  map_init_helper(T& d) : data(d) {}
  map_init_helper& operator() (typename T::key_type const& key,
                               typename T::mapped_type const& value) {
    data[key] = value;
    return *this;
  }
};

template<typename T> map_init_helper<T> map_init(T& item)
{ return map_init_helper<T>(item); }


} // namespace DPP
} // namespace llvm

#endif // LLVM_SUPPORT_H
