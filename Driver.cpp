//===- Driver.cpp ---------------------------------------------------===//
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

#include <Common/Box.hpp>
#include <Common/Map.hpp>
#include <Common/String.hpp>
#include <Common/Vec.hpp>
#include <fmt/format.h>

using namespace exi;

int main() {
  auto five = Box<int>::From(5);
  exi_assert(*five == 5);

  Str S = "Hello ";
  auto wrld = Box<Str>::FromIn("World!", Allocator<Str>());

  Allocator<Str> A;
  Str* ptr = A.allocate(1);
  A.construct(ptr, " This works!");
  auto wrks = Box<Str>::FromRaw(ptr, std::move(A));

  fmt::println("{}{}{}",
    S, *wrld, wrks->c_str()
  );
}
