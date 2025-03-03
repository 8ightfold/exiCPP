//===- Support/ManagedStatic.hpp -------------------------------------===//
//
// MODIFIED FOR THE PURPOSES OF THE EXICPP LIBRARY.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------===//
//
// Copyright (C) 2024 Eightfold
//
// Relicensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
//     limitations under the License.
//
//===----------------------------------------------------------------===//
//
// This file defines the ManagedStatic class and the exi_shutdown() function.
//
//===----------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstddef>

namespace exi {

/// object_creator - Helper method for ManagedStatic.
template <class C> struct object_creator {
  static void *call() { return new C(); }
};

/// object_deleter - Helper method for ManagedStatic.
///
template <typename T> struct object_deleter {
  static void call(void *Ptr) { delete (T *)Ptr; }
};
template <typename T, size_t N> struct object_deleter<T[N]> {
  static void call(void *Ptr) { delete[](T *)Ptr; }
};

// ManagedStatic must be initialized to zero, and it must *not* have a dynamic
// initializer because managed statics are often created while running other
// dynamic initializers. In standard C++11, the best way to accomplish this is
// with a constexpr default constructor. However, different versions of the
// Visual C++ compiler have had bugs where, even though the constructor may be
// constexpr, a dynamic initializer may be emitted depending on optimization
// settings. For the affected versions of MSVC, use the old linker
// initialization pattern of not providing a constructor and leaving the fields
// uninitialized. See http://llvm.org/PR41367 for details.
#if !defined(_MSC_VER) || (_MSC_VER >= 1925) || defined(__clang__)
# define EXI_USE_CONSTEXPR_CTOR
#endif

/// ManagedStaticBase - Common base class for ManagedStatic instances.
class ManagedStaticBase {
protected:
#ifdef EXI_USE_CONSTEXPR_CTOR
  mutable std::atomic<void *> Ptr{};
  mutable void (*DeleterFn)(void *) = nullptr;
  mutable const ManagedStaticBase *Next = nullptr;
#else
  // This should only be used as a static variable, which guarantees that this
  // will be zero initialized.
  mutable std::atomic<void *> Ptr;
  mutable void (*DeleterFn)(void *);
  mutable const ManagedStaticBase *Next;
#endif

  void RegisterManagedStatic(void *(*creator)(), void (*deleter)(void*)) const;

public:
#ifdef EXI_USE_CONSTEXPR_CTOR
  constexpr ManagedStaticBase() = default;
#endif

  /// isConstructed - Return true if this object has not been created yet.
  bool isConstructed() const { return Ptr != nullptr; }

  void destroy() const;
};

/// ManagedStatic - This transparently changes the behavior of global statics to
/// be lazily constructed on demand, and for making destruction be explicit 
/// through the exi_shutdown() function call.
template <class C, class Creator = object_creator<C>,
          class Deleter = object_deleter<C>>
class ManagedStatic : public ManagedStaticBase {
public:
  // Accessors.
  C &operator*() {
    void *Tmp = Ptr.load(std::memory_order_acquire);
    if (!Tmp)
      RegisterManagedStatic(&Creator::call, &Deleter::call);

    return *static_cast<C *>(Ptr.load(std::memory_order_relaxed));
  }

  C *operator->() { return &**this; }

  const C &operator*() const {
    void *Tmp = Ptr.load(std::memory_order_acquire);
    if (!Tmp)
      RegisterManagedStatic(&Creator::call, &Deleter::call);

    return *static_cast<C *>(Ptr.load(std::memory_order_relaxed));
  }

  const C *operator->() const { return &**this; }

  // Extract the instance, leaving the ManagedStatic uninitialized. The
  // user is then responsible for the lifetime of the returned instance.
  C *claim() {
    return static_cast<C *>(Ptr.exchange(nullptr));
  }
};

/// exi_shutdown - Deallocate and destroy all ManagedStatic variables.
void exi_shutdown();

/// exi_shutdown_obj - This is a simple helper class that calls
/// exi_shutdown() when it is destroyed.
struct exi_shutdown_obj {
  exi_shutdown_obj() = default;
  ~exi_shutdown_obj() { exi_shutdown(); }
};

} // namespace exi
