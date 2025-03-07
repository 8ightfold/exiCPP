//===- Logging.hpp --------------------------------------------------===//
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
///
/// \file
/// This file declares an interface for logging activity.
///
//===----------------------------------------------------------------===//

#pragma once

#include <Fundamental.hpp>

#if HAS_ATTR(format)
# define LOG_ATTR(...) __attribute__((format(__VA_ARGS__)))
#else
# define LOG_ATTR(...)
#endif

#define PRINTF_ATTR LOG_ATTR(printf, 1, 2)

namespace re {

PRINTF_ATTR void MiTrace(const char* Fmt, ...);
PRINTF_ATTR void MiWarn(const char* Fmt, ...);
PRINTF_ATTR void MiError(const char* Fmt, ...);

#if RE_DEBUG_EXTRA
# define MiTraceEx(...) MiTrace(__VA_ARGS__)
# define MiWarnEx(...)  MiWarn(__VA_ARGS__)
# define MiErrorEx(...) MiError(__VA_ARGS__)
#else
# define MiTraceEx(...) (void(0))
# define MiWarnEx(...)  (void(0))
# define MiErrorEx(...) (void(0))
#endif

} // namespace re
