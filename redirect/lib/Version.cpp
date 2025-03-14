//===- Version.hpp --------------------------------------------------===//
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

#include <Version.hpp>
#include "NtImports.hpp"

using namespace re;

VersionTriple re::GetVersionTriple(void) {
  DWORD Major = 10;
  DWORD Minor = 0;
  DWORD Build = 0;

  RtlGetNtVersionNumbers(&Major, &Minor, &Build);
  return {Major, Minor, u32(Build) & 0xFFFF};
}
