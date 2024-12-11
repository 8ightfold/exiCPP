//===- Support/ErrorHandle.cpp --------------------------------------===//
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

#include <Support/ErrorHandle.hpp>
#include <Common/StrRef.hpp>
#include <Support/_IO.hpp>
#include <cstring>
#include <cstdlib>
#include <fmt/format.h>
#if EXI_EXCEPTIONS
# include <new>
# include <stdexcept>
#endif

using namespace exi;

static const char* getAssertionMessage(H::AssertionKind kind) {
  using enum H::AssertionKind;
  switch (kind) {
   case ASK_Assert:
    return "Assertion failed";
   case ASK_Invariant:
    return "Invariant failed";
   case ASK_Unreachable:
    return "Unreachable reached";
  }
  return "??? failed";
}

[[noreturn]] void exi::report_fatal_error(StrRef msg, bool genCrashDiag) {
  std::string fullMsg = fmt::format("EXICPP ERROR: {}\n", msg);
  (void)::write(2, fullMsg.data(), fullMsg.size());

  if (genCrashDiag)
    std::abort();
  else
    std::exit(1);
}

[[noreturn]] void exi::fatal_alloc_error([[maybe_unused]] const char* msg) {
#if EXI_EXCEPTIONS
  throw std::bad_alloc();
#else
  if (msg == nullptr || msg[0] == '\0')
    msg = "Allocation failed.";
  
  const char* oom = "ERROR: Out of memory.\n";
  (void)::write(2, oom, std::strlen(oom));
  (void)::write(2, msg, std::strlen(msg));
  (void)::write(2, "\n", 1);

  std::abort();
#endif
}

[[noreturn]] void exi::exi_assert_impl(
 H::AssertionKind kind, const char* msg,
 const char* file, unsigned line
) {
  auto* const pre = getAssertionMessage(kind);
  if (msg && msg[0])
    fmt::println(stderr, "{}: \"{}\"", pre, msg);
  else
    fmt::print(stderr, "{}", pre);
  
  if (file)
    fmt::print(stderr, " at {}:{}", file, line);
  fmt::println("!");
  std::abort();

#ifdef EXI_UNREACHABLE
  EXI_UNREACHABLE;
#endif
}
