//===- Support/Program.hpp -------------------------------------------===//
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
// This file declares the sys::Program class.
//
//===----------------------------------------------------------------===//

#pragma once

#include <Common/ArrayRef.hpp>
#include <Common/Option.hpp>
#include <Common/StrRef.hpp>
#include <Support/ErrorOr.hpp>
#include <Support/Filesystem.hpp>
#include <chrono>
#include <system_error>

namespace exi {

class BitVector;

namespace sys {

/// This is the OS-specific separator for PATH like environment variables:
// a colon on Unix or a semicolon on Windows.
#if EXI_ON_UNIX
const char EnvPathSeparator = ':';
#elif defined (_WIN32)
const char EnvPathSeparator = ';';
#endif

#if defined(_WIN32)
typedef unsigned long procid_t; // Must match the type of DWORD on Windows.
typedef void *process_t;        // Must match the type of HANDLE on Windows.
#else
typedef ::pid_t procid_t;
typedef procid_t process_t;
#endif

/// This struct encapsulates information about a process.
struct ProcessInfo {
  enum : procid_t { InvalidPid = 0 };

  procid_t Pid;      /// The process identifier.
  process_t Process; /// Platform-dependent process object.

  /// The return code, set after execution.
  int ReturnCode;

  ProcessInfo();
};

/// This struct encapsulates information about a process execution.
struct ProcessStatistics {
  std::chrono::microseconds TotalTime;
  std::chrono::microseconds UserTime;
  u64 PeakMemory = 0; ///< Maximum resident set size in KiB.
};

/// Find the first executable file \p Name in \p Paths.
///
/// This does not perform hashing as a shell would but instead stats each PATH
/// entry individually so should generally be avoided. Core LLVM library
/// functions and options should instead require fully specified paths.
///
/// \param Name name of the executable to find. If it contains any system
///   slashes, it will be returned as is.
/// \param Paths optional list of paths to search for \p Name. If empty it
///   will use the system PATH environment instead.
///
/// \returns The fully qualified path to the first \p Name in \p Paths if it
///   exists. \p Name if \p Name has slashes in it. Otherwise an error.
ErrorOr<String> findProgramByName(StrRef Name, ArrayRef<StrRef> Paths = {});

// These functions change the specified standard stream (stdin or stdout) mode
// based on the Flags. They return errc::success if the specified stream was
// changed. Otherwise, a platform dependent error is returned.
std::error_code ChangeStdinMode(fs::OpenFlags Flags);
std::error_code ChangeStdoutMode(fs::OpenFlags Flags);

// These functions change the specified standard stream (stdin or stdout) to
// binary mode. They return errc::success if the specified stream
// was changed. Otherwise a platform dependent error is returned.
std::error_code ChangeStdinToBinary();
std::error_code ChangeStdoutToBinary();

/// This function executes the program using the arguments provided.  The
/// invoked program will inherit the stdin, stdout, and stderr file
/// descriptors, the environment and other configuration settings of the
/// invoking program.
/// This function waits for the program to finish, so should be avoided in
/// library functions that aren't expected to block. Consider using
/// ExecuteNoWait() instead.
/// \returns an integer result code indicating the status of the program.
/// A zero or positive value indicates the result code of the program.
/// -1 indicates failure to execute
/// -2 indicates a crash during execution or timeout
int ExecuteAndWait(
  StrRef Program, ///< Path of the program to be executed. It is
  ///< presumed this is the result of the findProgramByName method.
  ArrayRef<StrRef> Args, ///< An array of strings that are passed to the
  ///< program.  The first element should be the name of the program.
  ///< The array should **not** be terminated by an empty StrRef.
  Option<ArrayRef<StrRef>> Env =
      std::nullopt, ///< An optional vector of
  ///< strings to use for the program's environment. If not provided, the
  ///< current program's environment will be used.  If specified, the
  ///< vector should **not** be terminated by an empty StrRef.
  ArrayRef<Option<StrRef>> Redirects = {}, ///<
  ///< An array of optional paths. Should have a size of zero or three.
  ///< If the array is empty, no redirections are performed.
  ///< Otherwise, the inferior process's stdin(0), stdout(1), and stderr(2)
  ///< will be redirected to the corresponding paths, if the optional path
  ///< is present (not \c std::nullopt).
  ///< When an empty path is passed in, the corresponding file descriptor
  ///< will be disconnected (ie, /dev/null'd) in a portable way.
  unsigned SecondsToWait = 0, ///< If non-zero, this specifies the amount
  ///< of time to wait for the child process to exit. If the time
  ///< expires, the child is killed and this call returns. If zero,
  ///< this function will wait until the child finishes or forever if
  ///< it doesn't.
  unsigned MemoryLimit = 0, ///< If non-zero, this specifies max. amount
  ///< of memory can be allocated by process. If memory usage will be
  ///< higher limit, the child is killed and this call returns. If zero
  ///< - no memory limit.
  String *ErrMsg = nullptr, ///< If non-zero, provides a pointer to a
  ///< string instance in which error messages will be returned. If the
  ///< string is non-empty upon return an error occurred while invoking the
  ///< program.
  bool *ExecutionFailed = nullptr,
  Option<ProcessStatistics> *ProcStat = nullptr, ///< If non-zero,
  /// provides a pointer to a structure in which process execution
  /// statistics will be stored.
  BitVector *AffinityMask = nullptr ///< CPUs or processors the new
                                    /// program shall run on.
);

/// Similar to \ref ExecuteAndWait, but returns immediately.
/// \returns The \ref ProcessInfo of the newly launched process.
/// \note On Microsoft Windows systems, users will need to either call
/// \ref Wait until the process has finished executing or win32's CloseHandle
/// API on ProcessInfo.ProcessHandle to avoid memory leaks.
ProcessInfo ExecuteNoWait(
  StrRef Program, ArrayRef<StrRef> Args,
  Option<ArrayRef<StrRef>> Env,
  ArrayRef<Option<StrRef>> Redirects = {},
  unsigned MemoryLimit = 0, String *ErrMsg = nullptr,
  bool *ExecutionFailed = nullptr, BitVector *AffinityMask = nullptr,
  /// If true the executed program detatches from the controlling
  /// terminal. I/O streams such as exi::outs, exi::errs, and stdin will
  /// be closed until redirected to another output location
  bool DetachProcess = false
);

/// Return true if the given arguments fit within system-specific
/// argument length limits.
bool commandLineFitsWithinSystemLimits(StrRef Program,
                                       ArrayRef<StrRef> Args);

/// Return true if the given arguments fit within system-specific
/// argument length limits.
bool commandLineFitsWithinSystemLimits(StrRef Program,
                                       ArrayRef<const char *> Args);

/// File encoding options when writing contents that a non-UTF8 tool will
/// read (on Windows systems). For UNIX, we always use UTF-8.
enum WindowsEncodingMethod {
  /// UTF-8 is the LLVM native encoding, being the same as "do not perform
  /// encoding conversion".
  WEM_UTF8,
  WEM_CurrentCodePage,
  WEM_UTF16
};

/// Saves the UTF8-encoded \p contents string into the file \p FileName
/// using a specific encoding.
///
/// This write file function adds the possibility to choose which encoding
/// to use when writing a text file. On Windows, this is important when
/// writing files with internationalization support with an encoding that is
/// different from the one used in LLVM (UTF-8). We use this when writing
/// response files, since GCC tools on MinGW only understand legacy code
/// pages, and VisualStudio tools only understand UTF-16.
/// For UNIX, using different encodings is silently ignored, since all tools
/// work well with UTF-8.
/// This function assumes that you only use UTF-8 *text* data and will convert
/// it to your desired encoding before writing to the file.
///
/// FIXME: We use EM_CurrentCodePage to write response files for GNU tools in
/// a MinGW/MinGW-w64 environment, which has serious flaws but currently is
/// our best shot to make gcc/ld understand international characters. This
/// should be changed as soon as binutils fix this to support UTF16 on mingw.
///
/// \returns non-zero error_code if failed
std::error_code
writeFileWithEncoding(StrRef FileName, StrRef Contents,
                      WindowsEncodingMethod Encoding = WEM_UTF8);

/// This function waits for the process specified by \p PI to finish.
/// \returns A \see ProcessInfo struct with Pid set to:
/// \li The process id of the child process if the child process has changed
/// state.
/// \li 0 if the child process has not changed state.
/// \note Users of this function should always check the ReturnCode member of
/// the \see ProcessInfo returned from this function.
ProcessInfo Wait(
  const ProcessInfo &PI, ///< The child process that should be waited on.
  Option<unsigned> SecondsToWait, ///< If std::nullopt, waits until
  ///< child has terminated.
  ///< If a value, this specifies the amount of time to wait for the child
  ///< process. If the time expires, and \p Polling is false, the child is
  ///< killed and this < function returns. If the time expires and \p
  ///< Polling is true, the child is resumed.
  ///<
  ///< If zero, this function will perform a non-blocking
  ///< wait on the child process.
  String *ErrMsg = nullptr, ///< If non-zero, provides a pointer to a
  ///< string instance in which error messages will be returned. If the
  ///< string is non-empty upon return an error occurred while invoking the
  ///< program.
  Option<ProcessStatistics> *ProcStat =
      nullptr, ///< If non-zero, provides
  /// a pointer to a structure in which process execution statistics will
  /// be stored.

  bool Polling = false ///< If true, do not kill the process on timeout.
);

/// Print a command argument, and optionally quote it.
void printArg(exi::raw_ostream &OS, StrRef Arg, bool Quote);

#if defined(_WIN32)
/// Given a list of command line arguments, quote and escape them as necessary
/// to build a single flat command line appropriate for calling CreateProcess
/// on
/// Windows.
ErrorOr<WString> flattenWindowsCommandLine(ArrayRef<StrRef> Args);
#endif

} // namespace sys

} // namespace exi
