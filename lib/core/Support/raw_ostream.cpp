//===- Support/raw_ostream.cpp --------------------------------------===//
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
// This implements support for bulk buffered stream output.
//
//===----------------------------------------------------------------===//

#include <Support/raw_ostream.hpp>
#include <Common/Fundamental.hpp>
#include <Common/Twine.hpp>
#include <Common/StringExtras.hpp>
#include <Support/AutoConvert.hpp>
// #include "llvm/Support/Duration.h"
#include <Support/ErrorHandle.hpp>
#include <Support/Filesystem.hpp>
#include <Support/Format.hpp>
#include <Support/MathExtras.hpp>
#include <Support/NativeFormatting.hpp>
#include <Support/Process.hpp>
#include <Support/Program.hpp>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fmt/format.h>
#include <sys/stat.h>

// <fcntl.h> may provide O_BINARY.
#if __has_include(<fcntl.h>)
# include <fcntl.h>
#endif

#if __has_include(<unistd.h>)
# include <unistd.h>
#endif

#if defined(__CYGWIN__)
# include <io.h>
#endif

#ifdef _MSC_VER
#include <io.h>
#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO 2
#endif
#endif

#ifdef _WIN32
# include <Support/ConvertUTF.hpp>
# include <Support/Signals.hpp>
# include <Support/Windows/WindowsSupport.hpp>
#endif

using namespace exi;
using enum raw_ostream::Colors;

raw_ostream::~raw_ostream() {
  // raw_ostream's subclasses should take care to flush the buffer
  // in their destructors.
  exi_assert(OutBufCur == OutBufStart,
         "raw_ostream destructor called with non-empty buffer!");

  if (BufferMode == BufferKind::InternalBuffer)
    delete [] OutBufStart;
}

usize raw_ostream::preferred_buffer_size() const {
#ifdef _WIN32
  // On Windows BUFSIZ is only 512 which results in more calls to write. This
  // overhead can cause significant performance degradation. Therefore use a
  // better default.
  return (16 * 1024);
#else
  // BUFSIZ is intended to be a reasonable default.
  return BUFSIZ;
#endif
}

void raw_ostream::SetBuffered() {
  // Ask the subclass to determine an appropriate buffer size.
  if (usize Size = preferred_buffer_size())
    SetBufferSize(Size);
  else
    // It may return 0, meaning this stream should be unbuffered.
    SetUnbuffered();
}

void raw_ostream::SetBufferAndMode(char *BufferStart, usize Size,
                                   BufferKind Mode) {
  exi_assert(((Mode == BufferKind::Unbuffered && !BufferStart && Size == 0) ||
              (Mode != BufferKind::Unbuffered && BufferStart && Size != 0)),
              "stream must be unbuffered or have at least one byte");
  // Make sure the current buffer is free of content (we can't flush here; the
  // child buffer management logic will be in write_impl).
  exi_assert(GetNumBytesInBuffer() == 0, "Current buffer is non-empty!");

  if (BufferMode == BufferKind::InternalBuffer)
    delete [] OutBufStart;
  OutBufStart = BufferStart;
  OutBufEnd = OutBufStart+Size;
  OutBufCur = OutBufStart;
  BufferMode = Mode;

  exi_assert(OutBufStart <= OutBufEnd, "Invalid size!");
}

raw_ostream &raw_ostream::operator<<(unsigned long N) {
  exi::write_integer(*this, static_cast<u64>(N), 0, IntStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::operator<<(long N) {
  exi::write_integer(*this, static_cast<i64>(N), 0, IntStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::operator<<(unsigned long long N) {
  exi::write_integer(*this, static_cast<u64>(N), 0, IntStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::operator<<(long long N) {
  exi::write_integer(*this, static_cast<i64>(N), 0, IntStyle::Integer);
  return *this;
}

raw_ostream &raw_ostream::write_hex(unsigned long long N) {
  exi::write_hex(*this, N, HexPrintStyle::Lower);
  return *this;
}

raw_ostream &raw_ostream::operator<<(Colors C) {
  if (C == Colors::RESET)
    resetColor();
  else
    changeColor(C);
  return *this;
}

raw_ostream &raw_ostream::write_uuid(const uuid_t UUID) {
  for (int Idx = 0; Idx < 16; ++Idx) {
    *this << fmt::format("{:2X}", UUID[Idx]);
    if (Idx == 3 || Idx == 5 || Idx == 7 || Idx == 9)
      *this << "-";
  }
  return *this;
}


raw_ostream &raw_ostream::write_escaped(StrRef Str,
                                        bool UseHexEscapes) {
  for (unsigned char c : Str) {
    switch (c) {
    case '\\':
      *this << '\\' << '\\';
      break;
    case '\t':
      *this << '\\' << 't';
      break;
    case '\n':
      *this << '\\' << 'n';
      break;
    case '"':
      *this << '\\' << '"';
      break;
    default:
      if (isPrint(c)) {
        *this << c;
        break;
      }

      // Write out the escaped representation.
      if (UseHexEscapes) {
        *this << '\\' << 'x';
        *this << hexdigit((c >> 4) & 0xF);
        *this << hexdigit((c >> 0) & 0xF);
      } else {
        // Always use a full 3-character octal escape.
        *this << '\\';
        *this << char('0' + ((c >> 6) & 7));
        *this << char('0' + ((c >> 3) & 7));
        *this << char('0' + ((c >> 0) & 7));
      }
    }
  }

  return *this;
}

raw_ostream &raw_ostream::operator<<(const void *P) {
  exi::write_hex(*this, (uintptr_t)P, HexPrintStyle::PrefixLower);
  return *this;
}

raw_ostream &raw_ostream::operator<<(double N) {
  exi::write_double(*this, N, FloatStyle::Exponent);
  return *this;
}

void raw_ostream::flush_nonempty() {
  exi_assert(OutBufCur > OutBufStart, "Invalid call to flush_nonempty.");
  usize Length = OutBufCur - OutBufStart;
  OutBufCur = OutBufStart;
  write_impl(OutBufStart, Length);
}

raw_ostream &raw_ostream::write(unsigned char C) {
  // Group exceptional cases into a single branch.
  if EXI_UNLIKELY(OutBufCur >= OutBufEnd) {
    if EXI_UNLIKELY(!OutBufStart) {
      if (BufferMode == BufferKind::Unbuffered) {
        write_impl(reinterpret_cast<char *>(&C), 1);
        return *this;
      }
      // Set up a buffer and start over.
      SetBuffered();
      return write(C);
    }

    flush_nonempty();
  }

  *OutBufCur++ = C;
  return *this;
}

raw_ostream &raw_ostream::write(const char *Ptr, usize Size) {
  // Group exceptional cases into a single branch.
  if EXI_UNLIKELY(usize(OutBufEnd - OutBufCur) < Size) {
    if EXI_UNLIKELY(!OutBufStart) {
      if (BufferMode == BufferKind::Unbuffered) {
        write_impl(Ptr, Size);
        return *this;
      }
      // Set up a buffer and start over.
      SetBuffered();
      return write(Ptr, Size);
    }

    usize NumBytes = OutBufEnd - OutBufCur;

    // If the buffer is empty at this point we have a string that is larger
    // than the buffer. Directly write the chunk that is a multiple of the
    // preferred buffer size and put the remainder in the buffer.
    if EXI_UNLIKELY(OutBufCur == OutBufStart) {
      exi_assert(NumBytes != 0, "undefined behavior");
      usize BytesToWrite = Size - (Size % NumBytes);
      write_impl(Ptr, BytesToWrite);
      usize BytesRemaining = Size - BytesToWrite;
      if (BytesRemaining > usize(OutBufEnd - OutBufCur)) {
        // Too much left over to copy into our buffer.
        return write(Ptr + BytesToWrite, BytesRemaining);
      }
      copy_to_buffer(Ptr + BytesToWrite, BytesRemaining);
      return *this;
    }

    // We don't have enough space in the buffer to fit the string in. Insert as
    // much as possible, flush and start over with the remainder.
    copy_to_buffer(Ptr, NumBytes);
    flush_nonempty();
    return write(Ptr + NumBytes, Size - NumBytes);
  }

  copy_to_buffer(Ptr, Size);

  return *this;
}

void raw_ostream::copy_to_buffer(const char *Ptr, usize Size) {
  exi_assert(Size <= usize(OutBufEnd - OutBufCur), "Buffer overrun!");

  // Handle short strings specially, memcpy isn't very good at very short
  // strings.
  switch (Size) {
  case 4: OutBufCur[3] = Ptr[3]; [[fallthrough]];
  case 3: OutBufCur[2] = Ptr[2]; [[fallthrough]];
  case 2: OutBufCur[1] = Ptr[1]; [[fallthrough]];
  case 1: OutBufCur[0] = Ptr[0]; [[fallthrough]];
  case 0: break;
  default:
    std::memcpy(OutBufCur, Ptr, Size);
    break;
  }

  OutBufCur += Size;
}

// Formatted output.
raw_ostream &raw_ostream::operator<<(const IFormatObject& Fmt) {
  // If we have more than a few bytes left in our output buffer, try
  // formatting directly onto its end.
  usize NextBufferSize = 127;
  usize BufferBytesLeft = OutBufEnd - OutBufCur;
  if (BufferBytesLeft > 3) {
    usize BytesUsed = Fmt.print(OutBufCur, BufferBytesLeft);

    // Common case is that we have plenty of space.
    if (BytesUsed <= BufferBytesLeft) {
      OutBufCur += BytesUsed;
      return *this;
    }

    // Otherwise, we overflowed and the return value tells us the size to try
    // again with.
    NextBufferSize = BytesUsed;
  }

  // If we got here, we didn't have enough space in the output buffer for the
  // string.  Try printing into a SmallVec that is resized to have enough
  // space.  Iterate until we win.
  SmallVec<char, 128> V;

  while (true) {
    V.resize(NextBufferSize);

    // Try formatting into the SmallVec.
    usize BytesUsed = Fmt.print(V.data(), NextBufferSize);

    // If BytesUsed fit into the vector, we win.
    if (BytesUsed <= NextBufferSize)
      return write(V.data(), BytesUsed);

    // Otherwise, try again with a new size.
    exi_assert(BytesUsed > NextBufferSize, "Didn't grow buffer!?");
    NextBufferSize = BytesUsed;
  }
}

raw_ostream &raw_ostream::operator<<(const FormattedString &FS) {
  unsigned LeftIndent = 0;
  unsigned RightIndent = 0;
  const ssize_t Difference = FS.Width - FS.Str.size();
  if (Difference > 0) {
    switch (FS.Justify) {
    case FormattedString::JustifyNone:
      break;
    case FormattedString::JustifyLeft:
      RightIndent = Difference;
      break;
    case FormattedString::JustifyRight:
      LeftIndent = Difference;
      break;
    case FormattedString::JustifyCenter:
      LeftIndent = Difference / 2;
      RightIndent = Difference - LeftIndent;
      break;
    }
  }
  indent(LeftIndent);
  (*this) << FS.Str;
  indent(RightIndent);
  return *this;
}

raw_ostream &raw_ostream::operator<<(const FormattedNumber &FN) {
  if (FN.Hex) {
    HexPrintStyle Style;
    if (FN.Upper && FN.HexPrefix)
      Style = HexPrintStyle::PrefixUpper;
    else if (FN.Upper && !FN.HexPrefix)
      Style = HexPrintStyle::Upper;
    else if (!FN.Upper && FN.HexPrefix)
      Style = HexPrintStyle::PrefixLower;
    else
      Style = HexPrintStyle::Lower;
    exi::write_hex(*this, FN.HexValue, Style, FN.Width);
  } else {
    exi::SmallStr<16> Buffer;
    exi::raw_svector_ostream Stream(Buffer);
    exi::write_integer(Stream, FN.DecValue, 0, IntStyle::Integer);
    if (Buffer.size() < FN.Width)
      indent(FN.Width - Buffer.size());
    (*this) << Buffer;
  }
  return *this;
}

raw_ostream &raw_ostream::operator<<(const FormattedBytes &FB) {
  if (FB.Bytes.empty())
    return *this;

  usize LineIndex = 0;
  auto Bytes = FB.Bytes;
  const usize Size = Bytes.size();
  HexPrintStyle HPS = FB.Upper ? HexPrintStyle::Upper : HexPrintStyle::Lower;
  u64 OffsetWidth = 0;
  if (FB.FirstByteOffset) {
    // Figure out how many nibbles are needed to print the largest offset
    // represented by this data set, so that we can align the offset field
    // to the right width.
    usize Lines = Size / FB.NumPerLine;
    u64 MaxOffset = *FB.FirstByteOffset + Lines * FB.NumPerLine;
    unsigned Power = 0;
    if (MaxOffset > 0)
      Power = exi::Log2_64_Ceil(MaxOffset);
    OffsetWidth = std::max<u64>(4, exi::alignTo(Power, 4) / 4);
  }

  // The width of a block of data including all spaces for group separators.
  unsigned NumByteGroups =
      alignTo(FB.NumPerLine, FB.ByteGroupSize) / FB.ByteGroupSize;
  unsigned BlockCharWidth = FB.NumPerLine * 2 + NumByteGroups - 1;

  while (!Bytes.empty()) {
    indent(FB.IndentLevel);

    if (FB.FirstByteOffset) {
      u64 Offset = *FB.FirstByteOffset;
      exi::write_hex(*this, Offset + LineIndex, HPS, OffsetWidth);
      *this << ": ";
    }

    auto Line = Bytes.take_front(FB.NumPerLine);

    usize CharsPrinted = 0;
    // Print the hex bytes for this line in groups
    for (usize I = 0; I < Line.size(); ++I, CharsPrinted += 2) {
      if (I && (I % FB.ByteGroupSize) == 0) {
        ++CharsPrinted;
        *this << " ";
      }
      exi::write_hex(*this, Line[I], HPS, 2);
    }

    if (FB.ASCII) {
      // Print any spaces needed for any bytes that we didn't print on this
      // line so that the ASCII bytes are correctly aligned.
      assert(BlockCharWidth >= CharsPrinted);
      indent(BlockCharWidth - CharsPrinted + 2);
      *this << "|";

      // Print the ASCII char values for each byte on this line
      for (uint8_t Byte : Line) {
        if (isPrint(Byte))
          *this << static_cast<char>(Byte);
        else
          *this << '.';
      }
      *this << '|';
    }

    Bytes = Bytes.drop_front(Line.size());
    LineIndex += Line.size();
    if (LineIndex < Size)
      *this << '\n';
  }
  return *this;
}

template <char C>
static raw_ostream &write_padding(raw_ostream &OS, unsigned NumChars) {
  static const char Chars[] = {C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
                               C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C};

  // Usually the indentation is small, handle it with a fastpath.
  if (NumChars < std::size(Chars))
    return OS.write(Chars, NumChars);

  while (NumChars) {
    unsigned NumToWrite = std::min(NumChars, (unsigned)std::size(Chars) - 1);
    OS.write(Chars, NumToWrite);
    NumChars -= NumToWrite;
  }
  return OS;
}

/// indent - Insert 'NumSpaces' spaces.
raw_ostream &raw_ostream::indent(unsigned NumSpaces) {
  return write_padding<' '>(*this, NumSpaces);
}

/// write_zeros - Insert 'NumZeros' nulls.
raw_ostream &raw_ostream::write_zeros(unsigned NumZeros) {
  return write_padding<'\0'>(*this, NumZeros);
}

bool raw_ostream::prepare_colors() {
  // Colors were explicitly disabled.
  if (!ColorEnabled)
    return false;

#if EXI_HAS_SYS_IMPL
  // Colors require changing the terminal but this stream is not going to a
  // terminal.
  if (sys::Process::ColorNeedsFlush() && !is_displayed())
    return false;

  if (sys::Process::ColorNeedsFlush())
    flush();
#endif // EXI_HAS_SYS_IMPL

  return true;
}

raw_ostream::Colors raw_ostream::getColor(bool BG) const {
  if (!UsedColors)
    return Colors::RESET;
  return BG ? UsedColors->BG : UsedColors->FG;
}

raw_ostream::TiedColor raw_ostream::getTiedColor() const {
  return {getColor(false), getColor(true)};
}

void raw_ostream::setColor(enum Colors Color, bool BG) {
  if (!UsedColors || Color == SAVEDCOLOR)
    return;
  Colors& Col = *(BG ? &UsedColors->BG : &UsedColors->FG);
  Col = Color;
}

void raw_ostream::setColor(raw_ostream::TiedColor Tied) {
  this->setColor(Tied.FG, false);
  this->setColor(Tied.BG, true);
}

raw_ostream &raw_ostream::changeColor(enum Colors Color, bool Bold, bool BG) {
  if (!prepare_colors())
    return *this;
  this->setColor(Color, BG);
#if EXI_HAS_SYS_IMPL
  const char *colorcode =
      (Color == SAVEDCOLOR)
          ? sys::Process::OutputBold(BG)
          : sys::Process::OutputColor(static_cast<char>(Color), Bold, BG);
  if (colorcode) {
#if 0
    if (*colorcode == '\033') {
      const auto Len = std::strlen(colorcode);
      this->write(colorcode, Len);
      this->write(colorcode + 1, Len - 1);
      this->write("\033[0m", sizeof("\033[0m") - 1);
      return *this;
    }
#endif
    this->write(colorcode, std::strlen(colorcode));
  }
#endif // EXI_HAS_SYS_IMPL
  return *this;
}

raw_ostream &raw_ostream::changeColor(TiedColor Color, bool Bold) {
  return this->changeColor(Color.FG, Bold, false)
              .changeColor(Color.BG, Bold, true);
}

raw_ostream &raw_ostream::resetColor() {
  if (!prepare_colors())
    return *this;
  UsedColors.reset();
#if EXI_HAS_SYS_IMPL
  if (const char *colorcode = sys::Process::ResetColor())
    this->write(colorcode, strlen(colorcode));
#endif // EXI_HAS_SYS_IMPL
  return *this;
}

raw_ostream &raw_ostream::reverseColor() {
  if (!prepare_colors())
    return *this;
  if (UsedColors)
    std::swap(UsedColors->FG, UsedColors->BG);
#if EXI_HAS_SYS_IMPL
  if (const char *colorcode = sys::Process::OutputReverse())
    this->write(colorcode, strlen(colorcode));
#endif // EXI_HAS_SYS_IMPL
  return *this;
}

void raw_ostream::bindColor(raw_ostream &OS) {
  if (!OS.has_colors())
    return;
  // Bind up the chain.
  UsedColors = OS.UsedColors.value_or(OS.MyColors);
}

void raw_ostream::anchor() {}

//===----------------------------------------------------------------------===//
//  Formatted Output
//===----------------------------------------------------------------------===//

#if 0
// Out of line virtual method.
void IFormatObject::home() {
}
#endif

//===----------------------------------------------------------------------===//
//  raw_fd_ostream
//===----------------------------------------------------------------------===//

static int getFD(StrRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp, sys::fs::FileAccess Access,
                 sys::fs::OpenFlags Flags) {
  exi_assert(Access & sys::fs::FA_Write,
         "Cannot make a raw_ostream from a read-only descriptor!");

  // Handle "-" as stdout. Note that when we do this, we consider ourself
  // the owner of stdout and may set the "binary" flag globally based on Flags.
  if (Filename == "-") {
    EC = std::error_code();
    // Change stdout's text/binary mode based on the Flags.
    sys::ChangeStdoutMode(Flags);
    return STDOUT_FILENO;
  }

  int FD;
  if (Access & sys::fs::FA_Read)
    EC = sys::fs::openFileForReadWrite(Filename, FD, Disp, Flags);
  else
    EC = sys::fs::openFileForWrite(Filename, FD, Disp, Flags);
  if (EC)
    return -1;

  return FD;
}

raw_fd_ostream::raw_fd_ostream(StrRef Filename, std::error_code &EC)
    : raw_fd_ostream(Filename, EC, sys::fs::CD_CreateAlways, sys::fs::FA_Write,
                     sys::fs::OF_None) {}

raw_fd_ostream::raw_fd_ostream(StrRef Filename, std::error_code &EC,
                               sys::fs::CreationDisposition Disp)
    : raw_fd_ostream(Filename, EC, Disp, sys::fs::FA_Write, sys::fs::OF_None) {}

raw_fd_ostream::raw_fd_ostream(StrRef Filename, std::error_code &EC,
                               sys::fs::FileAccess Access)
    : raw_fd_ostream(Filename, EC, sys::fs::CD_CreateAlways, Access,
                     sys::fs::OF_None) {}

raw_fd_ostream::raw_fd_ostream(StrRef Filename, std::error_code &EC,
                               sys::fs::OpenFlags Flags)
    : raw_fd_ostream(Filename, EC, sys::fs::CD_CreateAlways, sys::fs::FA_Write,
                     Flags) {}

raw_fd_ostream::raw_fd_ostream(StrRef Filename, std::error_code &EC,
                               sys::fs::CreationDisposition Disp,
                               sys::fs::FileAccess Access,
                               sys::fs::OpenFlags Flags)
    : raw_fd_ostream(getFD(Filename, EC, Disp, Access, Flags), true) {}

/// FD is the file descriptor that this writes to.  If ShouldClose is true, this
/// closes the file when the stream is destroyed.
raw_fd_ostream::raw_fd_ostream(int fd, bool shouldClose, bool unbuffered,
                               OStreamKind K)
    : raw_pwrite_stream(unbuffered, K), FD(fd), ShouldClose(shouldClose) {
  if (FD < 0) {
    ShouldClose = false;
    return;
  }

  enable_colors(true);

  // Do not attempt to close stdout or stderr. We used to try to maintain the
  // property that tools that support writing file to stdout should not also
  // write informational output to stdout, but in practice we were never able to
  // maintain this invariant. Many features have been added to LLVM and clang
  // (-fdump-record-layouts, optimization remarks, etc) that print to stdout, so
  // users must simply be aware that mixed output and remarks is a possibility.
  if (FD <= STDERR_FILENO)
    ShouldClose = false;

#ifdef _WIN32
  // Check if this is a console device. This is not equivalent to isatty.
  IsWindowsConsole =
      ::GetFileType((HANDLE)::_get_osfhandle(fd)) == FILE_TYPE_CHAR;
#endif

  // Get the starting position.
  off_t loc = ::lseek(FD, 0, SEEK_CUR);
  sys::fs::file_status Status;
  std::error_code EC = status(FD, Status);
  IsRegularFile = Status.type() == sys::fs::file_type::regular_file;
#ifdef _WIN32
  // MSVCRT's _lseek(SEEK_CUR) doesn't return -1 for pipes.
  SupportsSeeking = !EC && IsRegularFile;
#else
  SupportsSeeking = !EC && loc != (off_t)-1;
#endif
  if (!SupportsSeeking)
    pos = 0;
  else
    pos = static_cast<u64>(loc);
}

raw_fd_ostream::~raw_fd_ostream() {
  if (FD >= 0) {
    flush();
    if (ShouldClose) {
      if (auto EC = sys::Process::SafelyCloseFileDescriptor(FD))
        error_detected(EC);
    }
  }

#ifdef __MINGW32__
  // On mingw, global dtors should not call exit().
  // report_fatal_error() invokes exit(). We know report_fatal_error()
  // might not write messages to stderr when any errors were detected
  // on FD == 2.
  if (FD == 2) return;
#endif

  // If there are any pending errors, report them now. Clients wishing
  // to avoid report_fatal_error calls should check for errors with
  // has_error() and clear the error flag with clear_error() before
  // destructing raw_ostream objects which may have errors.
  if (has_error())
    report_fatal_error(Twine("IO failure on output stream: ") +
                           error().message(),
                       /*gen_crash_diag=*/false);
}

#if defined(_WIN32)
// The most reliable way to print unicode in a Windows console is with
// WriteConsoleW. To use that, first transcode from UTF-8 to UTF-16. This
// assumes that LLVM programs always print valid UTF-8 to the console. The data
// might not be UTF-8 for two major reasons:
// 1. The program is printing binary (-filetype=obj -o -), in which case it
// would have been gibberish anyway.
// 2. The program is printing text in a semi-ascii compatible codepage like
// shift-jis or cp1252.
//
// Most LLVM programs don't produce non-ascii text unless they are quoting
// user source input. A well-behaved LLVM program should either validate that
// the input is UTF-8 or transcode from the local codepage to UTF-8 before
// quoting it. If they don't, this may mess up the encoding, but this is still
// probably the best compromise we can make.
static bool write_console_impl(int FD, StrRef Data) {
  SmallVec<wchar_t, 256> WideText;

  // Fall back to ::write if it wasn't valid UTF-8.
  if (auto EC = sys::windows::UTF8ToUTF16(Data, WideText))
    return false;

  // On Windows 7 and earlier, WriteConsoleW has a low maximum amount of data
  // that can be written to the console at a time.
  usize MaxWriteSize = WideText.size();
  if (!RunningWindows8OrGreater())
    MaxWriteSize = 32767;

  usize WCharsWritten = 0;
  do {
    usize WCharsToWrite =
        std::min(MaxWriteSize, WideText.size() - WCharsWritten);
    DWORD ActuallyWritten;
    bool Success =
        ::WriteConsoleW((HANDLE)::_get_osfhandle(FD), &WideText[WCharsWritten],
                        WCharsToWrite, &ActuallyWritten,
                        /*Reserved=*/nullptr);

    // The most likely reason for WriteConsoleW to fail is that FD no longer
    // points to a console. Fall back to ::write. If this isn't the first loop
    // iteration, something is truly wrong.
    if (!Success)
      return false;

    WCharsWritten += ActuallyWritten;
  } while (WCharsWritten != WideText.size());
  return true;
}
#endif

void raw_fd_ostream::write_impl(const char *Ptr, usize Size) {
  if (TiedStream)
    TiedStream->flush();

  exi_assert(FD >= 0, "File already closed.");
  pos += Size;

#if defined(_WIN32)
  // If this is a Windows console device, try re-encoding from UTF-8 to UTF-16
  // and using WriteConsoleW. If that fails, fall back to plain write().
  if (IsWindowsConsole) {
    if (write_console_impl(FD, StrRef(Ptr, Size)))
      return;
  }
#endif

  // The maximum write size is limited to INT32_MAX. A write
  // greater than SSIZE_MAX is implementation-defined in POSIX,
  // and Windows _write requires 32 bit input.
  usize MaxWriteSize = INT32_MAX;

#if defined(__linux__)
  // It is observed that Linux returns EINVAL for a very large write (>2G).
  // Make it a reasonably small value.
  MaxWriteSize = 1024 * 1024 * 1024;
#endif

  do {
    usize ChunkSize = std::min(Size, MaxWriteSize);
    ssize_t ret = ::write(FD, Ptr, ChunkSize);

    if (ret < 0) {
      // If it's a recoverable error, swallow it and retry the write.
      //
      // Ideally we wouldn't ever see EAGAIN or EWOULDBLOCK here, since
      // raw_ostream isn't designed to do non-blocking I/O. However, some
      // programs, such as old versions of bjam, have mistakenly used
      // O_NONBLOCK. For compatibility, emulate blocking semantics by
      // spinning until the write succeeds. If you don't want spinning,
      // don't use O_NONBLOCK file descriptors with raw_ostream.
      if (errno == EINTR || errno == EAGAIN
#ifdef EWOULDBLOCK
          || errno == EWOULDBLOCK
#endif
          )
        continue;

#ifdef _WIN32
      // Windows equivalents of SIGPIPE/EPIPE.
      DWORD WinLastError = GetLastError();
      if (WinLastError == ERROR_BROKEN_PIPE ||
          (WinLastError == ERROR_NO_DATA && errno == EINVAL)) {
        exi::sys::CallOneShotPipeSignalHandler();
        errno = EPIPE;
      }
#endif
      // Otherwise it's a non-recoverable error. Note it and quit.
      error_detected(errnoAsErrorCode());
      break;
    }

    // The write may have written some or all of the data. Update the
    // size and buffer pointer to reflect the remainder that needs
    // to be written. If there are no bytes left, we're done.
    Ptr += ret;
    Size -= ret;
  } while (Size > 0);
}

void raw_fd_ostream::close() {
  assert(ShouldClose);
  ShouldClose = false;
  flush();
  if (auto EC = sys::Process::SafelyCloseFileDescriptor(FD))
    error_detected(EC);
  FD = -1;
}

u64 raw_fd_ostream::seek(u64 off) {
  exi_assert(SupportsSeeking, "Stream does not support seeking!");
  flush();
#ifdef _WIN32
  pos = ::_lseeki64(FD, off, SEEK_SET);
#else
  pos = ::lseek(FD, off, SEEK_SET);
#endif
  if (pos == (u64)-1)
    error_detected(errnoAsErrorCode());
  return pos;
}

void raw_fd_ostream::pwrite_impl(const char *Ptr, usize Size,
                                 u64 Offset) {
  u64 Pos = tell();
  this->seek(Offset);
  this->write(Ptr, Size);
  this->seek(Pos);
}

usize raw_fd_ostream::preferred_buffer_size() const {
#if defined(_WIN32)
  // Disable buffering for console devices. Console output is re-encoded from
  // UTF-8 to UTF-16 on Windows, and buffering it would require us to split the
  // buffer on a valid UTF-8 codepoint boundary. Terminal buffering is disabled
  // below on most other OSs, so do the same thing on Windows and avoid that
  // complexity.
  if (IsWindowsConsole)
    return 0;
  return raw_ostream::preferred_buffer_size();
#elif defined(__MVS__)
  // The buffer size on z/OS is defined with macro BUFSIZ, which can be
  // retrieved by invoking function raw_ostream::preferred_buffer_size().
  return raw_ostream::preferred_buffer_size();
#else
  exi_assert(FD >= 0, "File not yet open!");
  struct stat statbuf;
  if (fstat(FD, &statbuf) != 0)
    return 0;

  // If this is a terminal, don't use buffering. Line buffering
  // would be a more traditional thing to do, but it's not worth
  // the complexity.
  if (S_ISCHR(statbuf.st_mode) && is_displayed())
    return 0;
  // Return the preferred block size.
  return statbuf.st_blksize;
#endif
}

bool raw_fd_ostream::is_displayed() const {
  return sys::Process::FileDescriptorIsDisplayed(FD);
}

bool raw_fd_ostream::has_colors() const {
  if (!HasColors)
    HasColors = sys::Process::FileDescriptorHasColors(FD);
  return *HasColors;
}

Expected<sys::fs::FileLocker> raw_fd_ostream::lock() {
  std::error_code EC = sys::fs::lockFile(FD);
  if (!EC)
    return sys::fs::FileLocker(FD);
  return errorCodeToError(EC);
}

#if 0
Expected<sys::fs::FileLocker>
raw_fd_ostream::tryLockFor(Duration const& Timeout) {
  std::error_code EC = sys::fs::tryLockFile(FD, Timeout.getDuration());
  if (!EC)
    return sys::fs::FileLocker(FD);
  return errorCodeToError(EC);
}
#endif

void raw_fd_ostream::anchor() {}



//===----------------------------------------------------------------------===//
//  outs(), errs(), nulls()
//===----------------------------------------------------------------------===//

raw_fd_ostream &exi::outs() {
  // Set buffer settings to model stdout behavior.
  std::error_code EC;
#ifdef __MVS__
  EC = enablezOSAutoConversion(STDOUT_FILENO);
  assert(!EC);
#endif
  static raw_fd_ostream Str("-", EC, sys::fs::OF_None);
  assert(!EC);
  return Str;
}

raw_fd_ostream &exi::errs() {
  // Set standard error to be unbuffered.
#ifdef __MVS__
  std::error_code EC = enablezOSAutoConversion(STDERR_FILENO);
  assert(!EC);
#endif
  static raw_fd_ostream Str(STDERR_FILENO, false, true);
  return Str;
}

/// nulls() - This returns a reference to a raw_ostream which discards output.
raw_ostream &exi::nulls() {
  static raw_null_ostream Str;
  return Str;
}

//===----------------------------------------------------------------------===//
// File Streams
//===----------------------------------------------------------------------===//

raw_fd_stream::raw_fd_stream(StrRef Filename, std::error_code &EC)
    : raw_fd_ostream(getFD(Filename, EC, sys::fs::CD_CreateAlways,
                           sys::fs::FA_Write | sys::fs::FA_Read,
                           sys::fs::OF_None),
                     true, false, OStreamKind::OK_FDStream) {
  if (EC)
    return;

  if (!isRegularFile())
    EC = std::make_error_code(std::errc::invalid_argument);
}

raw_fd_stream::raw_fd_stream(int fd, bool shouldClose)
    : raw_fd_ostream(fd, shouldClose, false, OStreamKind::OK_FDStream) {}

ssize_t raw_fd_stream::read(char *Ptr, usize Size) {
  exi_assert(get_fd() >= 0, "File already closed.");
  ssize_t Ret = ::read(get_fd(), (void *)Ptr, Size);
  if (Ret >= 0)
    inc_pos(Ret);
  else
    error_detected(errnoAsErrorCode());
  return Ret;
}

bool raw_fd_stream::classof(const raw_ostream *OS) {
  return OS->get_kind() == OStreamKind::OK_FDStream;
}

//===----------------------------------------------------------------------===//
//  raw_string_ostream
//===----------------------------------------------------------------------===//

void raw_string_ostream::write_impl(const char *Ptr, usize Size) {
  OS.append(Ptr, Size);
}

//===----------------------------------------------------------------------===//
//  raw_svector_ostream
//===----------------------------------------------------------------------===//

u64 raw_svector_ostream::current_pos() const { return OS.size(); }

void raw_svector_ostream::write_impl(const char *Ptr, usize Size) {
  OS.append(Ptr, Ptr + Size);
}

void raw_svector_ostream::pwrite_impl(const char *Ptr, usize Size,
                                      u64 Offset) {
  std::memcpy(OS.data() + Offset, Ptr, Size);
}

bool raw_svector_ostream::classof(const raw_ostream *OS) {
  return OS->get_kind() == OStreamKind::OK_SVecStream;
}

//===----------------------------------------------------------------------===//
//  raw_null_ostream
//===----------------------------------------------------------------------===//

GCC_IGNORED("-Wunused-parameter")

raw_null_ostream::~raw_null_ostream() {
#ifndef NDEBUG
  // ~raw_ostream asserts that the buffer is empty. This isn't necessary
  // with raw_null_ostream, but it's better to have raw_null_ostream follow
  // the rules than to change the rules just for raw_null_ostream.
  flush();
#endif
}

void raw_null_ostream::write_impl(const char *Ptr, usize Size) {
}

u64 raw_null_ostream::current_pos() const {
  return 0;
}

void raw_null_ostream::pwrite_impl(const char *Ptr, usize Size, u64 Offset) {
}

void raw_pwrite_stream::anchor() {}

void buffer_ostream::anchor() {}

void buffer_unique_ostream::anchor() {}

Error exi::writeToOutput(StrRef OutputFileName,
                         std::function<Error(raw_ostream &)> Write) {
  if (OutputFileName == "-")
    return Write(outs());

  if (OutputFileName == "/dev/null") {
    raw_null_ostream Out;
    return Write(Out);
  }

  unsigned Mode = sys::fs::all_read | sys::fs::all_write;
  Expected<sys::fs::TempFile> Temp =
      sys::fs::TempFile::create(OutputFileName + ".temp-stream-%%%%%%", Mode);
  if (!Temp)
    return createFileError(OutputFileName, Temp.takeError());

  raw_fd_ostream Out(Temp->FD, false);

  if (Error E = Write(Out)) {
    if (Error DiscardError = Temp->discard())
      return joinErrors(std::move(E), std::move(DiscardError));
    return E;
  }
  Out.flush();

  return Temp->keep(OutputFileName);
}
