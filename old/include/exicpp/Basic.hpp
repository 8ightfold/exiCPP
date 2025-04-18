//===- exicpp/Basic.hpp ---------------------------------------------===//
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
//
//  Defines types used by most parts of the program.
//
//===----------------------------------------------------------------===//

#pragma once

#ifndef EXIP_BASIC_HPP
#define EXIP_BASIC_HPP

#include "Features.hpp"
#include "Debug/CheckFlags.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <exip/procTypes.h>

namespace exi {

/// Defined as `char` most of the time.
using Char = exip::CharType;
using exip::Index;

#if EXIP_USE_MIMALLOC
template <typename T>
using Allocator = mi_stl_allocator<T>;
#else
template <typename T>
using Allocator = std::allocator<T>;
#endif

/// An owning array of `Char`s.
using Str = std::basic_string<
  Char, std::char_traits<Char>,
  Allocator<Char>
>;

/// A non-owning span over an array of `Char`s.
using StrRef = std::basic_string_view<Char>;

/// Alias for `std::optional`.
template <typename T>
using Option = std::optional<T>;

/// A non-owning span over an array of `char`s, may be the same as `StrRef`.
using AsciiStrRef = std::basic_string_view<char>;
using BinarySpan  = AsciiStrRef;

using CBinaryBuffer = exip::BinaryBuffer;
using CQName  = exip::QName;
using CString = exip::String;
using COptions = exip::EXIOptions;

} // namespace exi

#endif // EXIP_BASIC_HPP
