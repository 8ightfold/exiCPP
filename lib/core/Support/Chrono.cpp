//===- Support/Chrono.hpp -------------------------------------------===//
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

#include <Support/Chrono.hpp>
#include <Common/Features.hpp>
#include <Support/raw_ostream.hpp>
#include <fmt/format.h>
#include <fmt/chrono.h>

namespace exi {

using namespace sys;

const char exi::H::unit<std::ratio<3600>>::value[] = "h";
const char exi::H::unit<std::ratio<60>>::value[] = "m";
const char exi::H::unit<std::ratio<1>>::value[] = "s";
const char exi::H::unit<std::milli>::value[] = "ms";
const char exi::H::unit<std::micro>::value[] = "us";
const char exi::H::unit<std::nano>::value[] = "ns";

//======================================================================//
// Print Implementation
//======================================================================//

raw_ostream& print_time(raw_ostream& OS, double D, StrRef Unit) {
  return OS << D << Unit;
}

raw_ostream& print_time(raw_ostream& OS, intmax_t V, StrRef Unit) {
  return OS << V << Unit;
}

} // namespace exi
