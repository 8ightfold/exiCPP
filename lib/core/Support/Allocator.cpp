//===- Support/Allocator.cpp -----------------------------------------===//
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
// This file implements the BumpPtrAllocator interface.
//
//===----------------------------------------------------------------===//

#include <Support/Allocator.hpp>
#include <Support/raw_ostream.hpp>

namespace exi {

void H::printBumpPtrAllocatorStats(unsigned NumSlabs, usize BytesAllocated,
                                usize TotalMemory) {
  errs() << "\nNumber of memory regions: " << NumSlabs << '\n'
         << "Bytes used: " << BytesAllocated << '\n'
         << "Bytes allocated: " << TotalMemory << '\n'
         << "Bytes wasted: " << (TotalMemory - BytesAllocated)
         << " (includes alignment, etc)\n";
}

void PrintRecyclerStats(usize Size,
                        usize Align,
                        usize FreeListSize) {
  errs() << "Recycler element size: " << Size << '\n'
         << "Recycler element alignment: " << Align << '\n'
         << "Number of elements free for recycling: " << FreeListSize << '\n';
}

} // namespace exi
