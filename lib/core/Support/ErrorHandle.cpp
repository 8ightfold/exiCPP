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
#include <Common/String.hpp>
#include <Common/Twine.hpp>
#include <Support/_IO.hpp>
#include <Support/Errc.hpp>
#include <Support/FmtBuffer.hpp>
#include <Support/WindowsError.hpp>
#include <cstring>
#include <cstdlib>
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

static FmtBuffer::WriteState formatFatalError(FmtBuffer& buf, StrRef S) {
  const auto out = buf.format("EXICPP ERROR: {}\n", S);
  if (out != FmtBuffer::FullWrite)
    return buf.setLast('\n');
  return out;
}

[[noreturn]] void exi::report_fatal_error(const char* msg, bool genCrashDiag) {
  exi::report_fatal_error(Twine(msg), genCrashDiag);
}

[[noreturn]] void exi::report_fatal_error(StrRef msg, bool genCrashDiag) {
  exi::report_fatal_error(Twine(msg), genCrashDiag);
}

[[noreturn]] void exi::report_fatal_error(const Twine& msg, bool genCrashDiag) {
  StaticFmtBuffer<512> fullMsg;
  if (msg.isSingleStrRef()) {
    // Trivial path, just grab the StrRef.
    formatFatalError(fullMsg, msg.getSingleStrRef());
  } else {
    SmallVec<char, 256> tmpVec;
    formatFatalError(fullMsg, msg.toStrRef(tmpVec));
  }
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
  if (file)
    fmt::print(stderr, "\nAt \"{}:{}\":\n  ", file, line);
  
  if (msg && msg[0])
    fmt::print(stderr, "{}: {}", pre, msg);
  else
    fmt::print(stderr, "{}", pre);
    
  fmt::println(stderr, ".");
  std::abort();

#ifdef EXI_UNREACHABLE
  EXI_UNREACHABLE;
#endif
}



#ifdef _WIN32

#define WIN32_NO_STATUS
#include <Support/Windows/WindowsSupport.hpp>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winerror.h>

// This is equivalent to NtCurrentTeb()->LastStatusValue, but the public
// _TEB definition does not expose the LastStatusValue field directly.
// Avoid offsetting into this structure by calling RtlGetLastNtStatus
// from ntdll.dll.
//
// The return of this function will roughly match that of
// GetLastError, but this lower level API disambiguates some cases
// that GetLastError does not.
//
// For more information, see:
// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/api/pebteb/teb/index.htm
// https://github.com/llvm/llvm-project/issues/89137
extern "C" NTSYSAPI NTSTATUS NTAPI RtlGetLastNtStatus(void);

// This function obtains the last error code and maps it. It may call
// RtlGetLastNtStatus, which is a lower level API that can return a
// more specific error code than GetLastError.
std::error_code exi::mapLastWindowsError() {
  unsigned EV = ::GetLastError();
  // The mapping of NTSTATUS to Win32 error loses some information; special
  // case the generic ERROR_ACCESS_DENIED code to check the underlying
  // NTSTATUS and potentially return a more accurate error code.
  if (EV == ERROR_ACCESS_DENIED) {
    exi::errc code = RtlGetLastNtStatus() == STATUS_DELETE_PENDING
                          ? errc::delete_pending
                          : errc::permission_denied;
    return make_error_code(code);
  }
  return mapWindowsError(EV);
}

// I'd rather not double the line count of the following.
#define MAP_ERR_TO_COND(x, y)                                                  \
  case x:                                                                      \
    return make_error_code(errc::y)

std::error_code exi::mapWindowsError(unsigned EV) {
  switch (EV) {
    MAP_ERR_TO_COND(ERROR_ACCESS_DENIED, permission_denied);
    MAP_ERR_TO_COND(ERROR_ALREADY_EXISTS, file_exists);
    MAP_ERR_TO_COND(ERROR_BAD_NETPATH, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_BAD_PATHNAME, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_BAD_UNIT, no_such_device);
    MAP_ERR_TO_COND(ERROR_BROKEN_PIPE, broken_pipe);
    MAP_ERR_TO_COND(ERROR_BUFFER_OVERFLOW, filename_too_long);
    MAP_ERR_TO_COND(ERROR_BUSY, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_BUSY_DRIVE, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_CANNOT_MAKE, permission_denied);
    MAP_ERR_TO_COND(ERROR_CANTOPEN, io_error);
    MAP_ERR_TO_COND(ERROR_CANTREAD, io_error);
    MAP_ERR_TO_COND(ERROR_CANTWRITE, io_error);
    MAP_ERR_TO_COND(ERROR_CURRENT_DIRECTORY, permission_denied);
    MAP_ERR_TO_COND(ERROR_DEV_NOT_EXIST, no_such_device);
    MAP_ERR_TO_COND(ERROR_DEVICE_IN_USE, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_DIR_NOT_EMPTY, directory_not_empty);
    MAP_ERR_TO_COND(ERROR_DIRECTORY, invalid_argument);
    MAP_ERR_TO_COND(ERROR_DISK_FULL, no_space_on_device);
    MAP_ERR_TO_COND(ERROR_FILE_EXISTS, file_exists);
    MAP_ERR_TO_COND(ERROR_FILE_NOT_FOUND, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_HANDLE_DISK_FULL, no_space_on_device);
    MAP_ERR_TO_COND(ERROR_INVALID_ACCESS, permission_denied);
    MAP_ERR_TO_COND(ERROR_INVALID_DRIVE, no_such_device);
    MAP_ERR_TO_COND(ERROR_INVALID_FUNCTION, function_not_supported);
    MAP_ERR_TO_COND(ERROR_INVALID_HANDLE, invalid_argument);
    MAP_ERR_TO_COND(ERROR_INVALID_NAME, invalid_argument);
    MAP_ERR_TO_COND(ERROR_INVALID_PARAMETER, invalid_argument);
    MAP_ERR_TO_COND(ERROR_LOCK_VIOLATION, no_lock_available);
    MAP_ERR_TO_COND(ERROR_LOCKED, no_lock_available);
    MAP_ERR_TO_COND(ERROR_NEGATIVE_SEEK, invalid_argument);
    MAP_ERR_TO_COND(ERROR_NOACCESS, permission_denied);
    MAP_ERR_TO_COND(ERROR_NOT_ENOUGH_MEMORY, not_enough_memory);
    MAP_ERR_TO_COND(ERROR_NOT_READY, resource_unavailable_try_again);
    MAP_ERR_TO_COND(ERROR_NOT_SUPPORTED, not_supported);
    MAP_ERR_TO_COND(ERROR_OPEN_FAILED, io_error);
    MAP_ERR_TO_COND(ERROR_OPEN_FILES, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_OUTOFMEMORY, not_enough_memory);
    MAP_ERR_TO_COND(ERROR_PATH_NOT_FOUND, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_READ_FAULT, io_error);
    MAP_ERR_TO_COND(ERROR_REPARSE_TAG_INVALID, invalid_argument);
    MAP_ERR_TO_COND(ERROR_RETRY, resource_unavailable_try_again);
    MAP_ERR_TO_COND(ERROR_SEEK, io_error);
    MAP_ERR_TO_COND(ERROR_SHARING_VIOLATION, permission_denied);
    MAP_ERR_TO_COND(ERROR_TOO_MANY_OPEN_FILES, too_many_files_open);
    MAP_ERR_TO_COND(ERROR_WRITE_FAULT, io_error);
    MAP_ERR_TO_COND(ERROR_WRITE_PROTECT, permission_denied);
    MAP_ERR_TO_COND(WSAEACCES, permission_denied);
    MAP_ERR_TO_COND(WSAEBADF, bad_file_descriptor);
    MAP_ERR_TO_COND(WSAEFAULT, bad_address);
    MAP_ERR_TO_COND(WSAEINTR, interrupted);
    MAP_ERR_TO_COND(WSAEINVAL, invalid_argument);
    MAP_ERR_TO_COND(WSAEMFILE, too_many_files_open);
    MAP_ERR_TO_COND(WSAENAMETOOLONG, filename_too_long);
  default:
    return std::error_code(EV, std::system_category());
  }
}

#endif
