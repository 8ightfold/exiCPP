//===- Support/MemoryBuffer.hpp --------------------------------------===//
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
//  This file defines the MemoryBuffer interface.
//
//===----------------------------------------------------------------===//

#pragma once

#include <Common/ArrayRef.hpp>
#include <Common/Box.hpp>
#include <Common/StrRef.hpp>
#include <Common/Twine.hpp>
#include <Support/Alignment.hpp>
#include <Support/ErrorOr.hpp>
#include <Support/MemoryBufferRef.hpp>
#include <cstddef>
#include <cstdint>

namespace exi {
namespace sys {
namespace fs {
// Duplicated from FileSystem.h to avoid a dependency.
#if defined(_WIN32)
// A Win32 HANDLE is a typedef of void*
using file_t = void *;
#else
using file_t = int;
#endif
} // namespace fs
} // namespace sys

/// This interface provides simple read-only access to a block of memory, and
/// provides simple methods for reading files and standard input into a memory
/// buffer.  In addition to basic access to the characters in the file, this
/// interface guarantees you can read one character past the end of the file,
/// and that this character will read as '\0'.
///
/// The '\0' guarantee is needed to support an optimization -- it's intended to
/// be more efficient for clients which are reading all the data to stop
/// reading when they encounter a '\0' than to continually check the file
/// position to see if it has reached the end of the file.
class MemoryBuffer {
  const char *BufferStart; // Start of the buffer.
  const char *BufferEnd;   // End of the buffer.

protected:
  MemoryBuffer() = default;

  void init(const char *BufStart, const char *BufEnd,
            bool RequiresNullTerminator);

public:
  MemoryBuffer(const MemoryBuffer &) = delete;
  MemoryBuffer &operator=(const MemoryBuffer &) = delete;
  virtual ~MemoryBuffer();

  const char *getBufferStart() const { return BufferStart; }
  const char *getBufferEnd() const   { return BufferEnd; }
  usize getBufferSize() const { return BufferEnd-BufferStart; }

  StrRef getBuffer() const {
    return StrRef(BufferStart, getBufferSize());
  }

  /// Checks if a pointer `Ptr` is located in this buffer.
  bool isInBuffer(const char* Ptr) const {
    return (Ptr >= BufferStart) && (Ptr < BufferEnd);
  }
  /// Gets the offset of a pointer in this buffer, otherwise `-1`.
  usize getBufferOffset(const char* Ptr) const {
    if EXI_UNLIKELY(!isInBuffer(Ptr))
      return StrRef::npos;
    return (Ptr - BufferStart);
  }

  /// Return an identifier for this buffer, typically the filename it was read
  /// from.
  virtual StrRef getBufferIdentifier() const { return "Unknown buffer"; }

  /// For read-only MemoryBuffer_MMap, mark the buffer as unused in the near
  /// future and the kernel can free resources associated with it. Further
  /// access is supported but may be expensive. This calls
  /// madvise(MADV_DONTNEED) on read-only file mappings on *NIX systems. This
  /// function should not be called on a writable buffer.
  virtual void dontNeedIfMmap() {}

  /// Open the specified file as a MemoryBuffer, returning a new MemoryBuffer
  /// if successful, otherwise returning null.
  ///
  /// \param IsText Set to true to indicate that the file should be read in
  /// text mode.
  ///
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static ErrorOr<Box<MemoryBuffer>>
  getFile(const Twine &Filename, bool IsText = false,
          bool RequiresNullTerminator = true, bool IsVolatile = false,
          Option<Align> Alignment = nullopt);

  /// Read all of the specified file into a MemoryBuffer as a stream
  /// (i.e. until EOF reached). This is useful for special files that
  /// look like a regular file but have 0 size (e.g. /proc/cpuinfo on Linux).
  static ErrorOr<Box<MemoryBuffer>>
  getFileAsStream(const Twine &Filename);

  /// Given an already-open file descriptor, map some slice of it into a
  /// MemoryBuffer. The slice is specified by an \p Offset and \p MapSize.
  /// Since this is in the middle of a file, the buffer is not null terminated.
  static ErrorOr<Box<MemoryBuffer>>
  getOpenFileSlice(sys::fs::file_t FD, const Twine &Filename, u64 MapSize,
                   i64 Offset, bool IsVolatile = false,
                   Option<Align> Alignment = nullopt);

  /// Given an already-open file descriptor, read the file and return a
  /// MemoryBuffer.
  ///
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static ErrorOr<Box<MemoryBuffer>>
  getOpenFile(sys::fs::file_t FD, const Twine &Filename, u64 FileSize,
              bool RequiresNullTerminator = true, bool IsVolatile = false,
              Option<Align> Alignment = nullopt);

  /// Open the specified memory range as a MemoryBuffer. Note that InputData
  /// must be null terminated if RequiresNullTerminator is true.
  static Box<MemoryBuffer>
  getMemBuffer(StrRef InputData, StrRef BufferName = "",
               bool RequiresNullTerminator = true);

  static Box<MemoryBuffer>
  getMemBuffer(MemoryBufferRef Ref, bool RequiresNullTerminator = true);

  /// Open the specified memory range as a MemoryBuffer, copying the contents
  /// and taking ownership of it. InputData does not have to be null terminated.
  static Box<MemoryBuffer>
  getMemBufferCopy(StrRef InputData, const Twine &BufferName = "");

  /// Read all of stdin into a file buffer, and return it.
  static ErrorOr<Box<MemoryBuffer>> getSTDIN();

  /// Open the specified file as a MemoryBuffer, or open stdin if the Filename
  /// is "-".
  static ErrorOr<Box<MemoryBuffer>>
  getFileOrSTDIN(const Twine &Filename, bool IsText = false,
                 bool RequiresNullTerminator = true,
                 Option<Align> Alignment = nullopt);

  /// Map a subrange of the specified file as a MemoryBuffer.
  static ErrorOr<Box<MemoryBuffer>>
  getFileSlice(const Twine &Filename, u64 MapSize, u64 Offset,
               bool IsVolatile = false,
               Option<Align> Alignment = nullopt);

  //===--------------------------------------------------------------------===//
  // Provided for performance analysis.
  //===--------------------------------------------------------------------===//

  /// The kind of memory backing used to support the MemoryBuffer.
  enum BufferKind {
    MemoryBuffer_Malloc,
    MemoryBuffer_MMap
  };

  /// Return information on the memory mechanism used to support the
  /// MemoryBuffer.
  virtual BufferKind getBufferKind() const = 0;

  MemoryBufferRef getMemBufferRef() const;
};

/// This class is an extension of MemoryBuffer, which allows copy-on-write
/// access to the underlying contents.  It only supports creation methods that
/// are guaranteed to produce a writable buffer.  For example, mapping a file
/// read-only is not supported.
class WritableMemoryBuffer : public MemoryBuffer {
protected:
  WritableMemoryBuffer() = default;

public:
  using MemoryBuffer::getBuffer;
  using MemoryBuffer::getBufferEnd;
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  MutArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  static ErrorOr<Box<WritableMemoryBuffer>>
  getFile(const Twine &Filename, bool IsVolatile = false,
          Option<Align> Alignment = nullopt);
  
  static ErrorOr<Box<WritableMemoryBuffer>>
  getFileEx(const Twine &Filename,
            bool RequiresNullTerminator = true, bool IsVolatile = false,
            Option<Align> Alignment = nullopt);
  
  // TODO: Remove? Prob
  static ErrorOr<Box<WritableMemoryBuffer>>
  getOpenFile(sys::fs::file_t FD, const Twine &Filename, u64 FileSize,
              bool RequiresNullTerminator = true, bool IsVolatile = false,
              Option<Align> Alignment = nullopt);

  /// Map a subrange of the specified file as a WritableMemoryBuffer.
  static ErrorOr<Box<WritableMemoryBuffer>>
  getFileSlice(const Twine &Filename, u64 MapSize, u64 Offset,
               bool IsVolatile = false,
               Option<Align> Alignment = nullopt);

  /// Allocate a new MemoryBuffer of the specified size that is not initialized.
  /// Note that the caller should initialize the memory allocated by this
  /// method. The memory is owned by the MemoryBuffer object.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static Box<WritableMemoryBuffer>
  getNewUninitMemBuffer(usize Size, const Twine &BufferName = "",
                        Option<Align> Alignment = nullopt);

  /// Allocate a new zero-initialized MemoryBuffer of the specified size. Note
  /// that the caller need not initialize the memory allocated by this method.
  /// The memory is owned by the MemoryBuffer object.
  static Box<WritableMemoryBuffer>
  getNewMemBuffer(usize Size, const Twine &BufferName = "");

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that they got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

/// This class is an extension of MemoryBuffer, which allows write access to
/// the underlying contents and committing those changes to the original source.
/// It only supports creation methods that are guaranteed to produce a writable
/// buffer.  For example, mapping a file read-only is not supported.
class WriteThroughMemoryBuffer : public MemoryBuffer {
protected:
  WriteThroughMemoryBuffer() = default;

public:
  using MemoryBuffer::getBuffer;
  using MemoryBuffer::getBufferEnd;
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  MutArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  static ErrorOr<Box<WriteThroughMemoryBuffer>>
  getFile(const Twine &Filename, i64 FileSize = -1);

  /// Map a subrange of the specified file as a ReadWriteMemoryBuffer.
  static ErrorOr<Box<WriteThroughMemoryBuffer>>
  getFileSlice(const Twine &Filename, u64 MapSize, u64 Offset);

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that they got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

} // namespace exi
