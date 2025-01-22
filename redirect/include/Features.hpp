//===- Features.hpp -------------------------------------------------===//
//
// Copyright (C) 2024 Eightfold
//
// Licensed under the Apache License, Version 2.0 (the "License");
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

#pragma once

// At `{TOP_LEVEL}/include/core/Config`.
#include <Config.inc>
#include <cstddef>

#if !EXI_ON_WIN32
# error This library should only be used on Windows!
#elif !defined(_WIN64)
# error This library does not target 32-bit Windows!
#endif

#ifdef __has_builtin
# define HAS_BUILTIN(x) __has_builtin(x)
#else
# define HAS_BUILTIN(x) 0
#endif

#ifdef __has_attribute
# define HAS_ATTR(x) __has_attribute(x)
#else
# define HAS_ATTR(x) 0
#endif

#if HAS_ATTR(always_inline)
# define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
# define ALWAYS_INLINE __forceinline
#else
# define ALWAYS_INLINE inline
#endif

#define DLL_EXPORT __declspec(dllexport)
#define DLL_IMPORT __declspec(dllimport)

#if HAS_BUILTIN(__builtin_expect)
# define RE_EXPECT(v, expr) \
 (__builtin_expect(static_cast<bool>(expr), v))
#else
# define RE_EXPECT(v, expr) (static_cast<bool>(expr))
#endif

#define LIKELY(...)   RE_EXPECT(1, (__VA_ARGS__))
#define UNLIKELY(...) RE_EXPECT(0, (__VA_ARGS__))

#if HAS_BUILTIN(__builtin_trap) || defined(__GNUC__)
# define RE_TRAP __builtin_trap()
#elif defined(_MSC_VER)
# define RE_TRAP __debugbreak()
#else
# define RE_TRAP *(volatile int*)0x11 = 0
#endif

#if EXI_DEBUG
# define re_assert(...) \
 (LIKELY(::re::re_assert_(__VA_ARGS__)) ? (void(0)) : (RE_TRAP))
#else
# define re_assert(...) (void(0))
#endif

#if __has_cpp_attribute(clang::musttail)
# define tail_return [[clang::musttail]] return
#else
# define tail_return return
#endif

namespace re {

ALWAYS_INLINE constexpr bool
 re_assert_(auto Expr, const char* Str = "") {
  return LIKELY(static_cast<bool>(Expr));
}

} // namespace re
