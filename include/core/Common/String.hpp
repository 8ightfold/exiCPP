//===- Common/String.hpp --------------------------------------------===//
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

#include <Support/Alloc.hpp>
#include <string>
#include <string_view>

#define EXI_CUSTOM_STRREF 0

namespace exi {

using char_t = char;
using Char   = char_t;
using CharTraits = std::char_traits<char_t>;

using Str  = std::basic_string<char_t, CharTraits, Allocator<char_t>>;
using WStr = std::wstring;

using StrRef  = std::basic_string_view<char_t, CharTraits>;
using WStrRef = std::wstring_view;

} // namespace exi
