//===- Support/Path.cpp ---------------------------------------------===//
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
//  This file implements the operating system Path API.
//
//===----------------------------------------------------------------===//

#include <Support/Path.hpp>
#include <Common/ArrayRef.hpp>
#include <Common/Fundamental.hpp>
#include <Common/ScopeExit.hpp>
#include <Common/STLExtras.hpp>
#include <Common/StringExtras.hpp>
#include <Support/Errc.hpp>
#include <Support/ErrorHandle.hpp>
#include <Support/Filesystem.hpp>
#include <Support/Process.hpp>
#include <Support/Signals.hpp>
#include <cctype>

#define EXI_WINDOWS_PREFER_FORWARD_SLASH 1

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

// For `TempFile::create`.
GCC_IGNORED("-Wredundant-move")

using namespace exi;
using namespace exi::support::endian;

namespace {
  using exi::StrRef;
  using exi::sys::path::is_separator;
  using exi::sys::path::Style;

  inline Style real_style(Style style) {
    if (style != Style::native)
      return style;
    if (is_style_posix(style))
      return Style::posix;
    return EXI_WINDOWS_PREFER_FORWARD_SLASH ? Style::windows_slash
                                            : Style::windows_backslash;
  }

  inline const char *separators(Style style) {
    if (is_style_windows(style))
      return "\\/";
    return "/";
  }

  inline char preferred_separator(Style style) {
    if (real_style(style) == Style::windows)
      return '\\';
    return '/';
  }

  StrRef find_first_component(StrRef path, Style style) {
    // Look for this first component in the following order.
    // * empty (in this case we return an empty string)
    // * either C: or {//,\\}net.
    // * {/,\}
    // * {file,directory}name

    if (path.empty())
      return path;

    if (is_style_windows(style)) {
      // C:
      if (path.size() >= 2 &&
          std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return path.substr(0, 2);
    }

    // //net
    if ((path.size() > 2) && is_separator(path[0], style) &&
        path[0] == path[1] && !is_separator(path[2], style)) {
      // Find the next directory separator.
      usize end = path.find_first_of(separators(style), 2);
      return path.substr(0, end);
    }

    // {/,\}
    if (is_separator(path[0], style))
      return path.substr(0, 1);

    // * {file,directory}name
    usize end = path.find_first_of(separators(style));
    return path.substr(0, end);
  }

  // Returns the first character of the filename in str. For paths ending in
  // '/', it returns the position of the '/'.
  usize filename_pos(StrRef str, Style style) {
    if (str.size() > 0 && is_separator(str[str.size() - 1], style))
      return str.size() - 1;

    usize pos = str.find_last_of(separators(style), str.size() - 1);

    if (is_style_windows(style)) {
      if (pos == StrRef::npos)
        pos = str.find_last_of(':', str.size() - 1);
    }

    if (pos == StrRef::npos || (pos == 1 && is_separator(str[0], style)))
      return 0;

    return pos + 1;
  }

  // Returns the position of the root directory in str. If there is no root
  // directory in str, it returns StrRef::npos.
  usize root_dir_start(StrRef str, Style style) {
    // case "c:/"
    if (is_style_windows(style)) {
      if (str.size() > 2 && str[1] == ':' && is_separator(str[2], style))
        return 2;
    }

    // case "//net"
    if (str.size() > 3 && is_separator(str[0], style) && str[0] == str[1] &&
        !is_separator(str[2], style)) {
      return str.find_first_of(separators(style), 2);
    }

    // case "/"
    if (str.size() > 0 && is_separator(str[0], style))
      return 0;

    return StrRef::npos;
  }

  // Returns the position past the end of the "parent path" of path. The parent
  // path will not end in '/', unless the parent is the root directory. If the
  // path has no parent, 0 is returned.
  usize parent_path_end(StrRef path, Style style) {
    usize end_pos = filename_pos(path, style);

    bool filename_was_sep =
        path.size() > 0 && is_separator(path[end_pos], style);

    // Skip separators until we reach root dir (or the start of the string).
    usize root_dir_pos = root_dir_start(path, style);
    while (end_pos > 0 &&
           (root_dir_pos == StrRef::npos || end_pos > root_dir_pos) &&
           is_separator(path[end_pos - 1], style))
      --end_pos;

    if (end_pos == root_dir_pos && !filename_was_sep) {
      // We've reached the root dir and the input path was *not* ending in a
      // sequence of slashes. Include the root dir in the parent path.
      return root_dir_pos + 1;
    }

    // Otherwise, just include before the last slash.
    return end_pos;
  }
} // end unnamed namespace

enum FSEntity {
  FS_Dir,
  FS_File,
  FS_Name
};

static std::error_code
createUniqueEntity(const Twine &Model, int &ResultFD,
                   SmallVecImpl<char> &ResultPath, bool MakeAbsolute,
                   FSEntity Type, sys::fs::OpenFlags Flags = sys::fs::OF_None,
                   unsigned Mode = 0) {

  // Limit the number of attempts we make, so that we don't infinite loop. E.g.
  // "permission denied" could be for a specific file (so we retry with a
  // different name) or for the whole directory (retry would always fail).
  // Checking which is racy, so we try a number of times, then give up.
  std::error_code EC;
  for (int Retries = 128; Retries > 0; --Retries) {
    sys::fs::createUniquePath(Model, ResultPath, MakeAbsolute);
    // Try to open + create the file.
    switch (Type) {
    case FS_File: {
      EC = sys::fs::openFileForReadWrite(Twine(ResultPath.begin()), ResultFD,
                                         sys::fs::CD_CreateNew, Flags, Mode);
      if (EC) {
        // errc::permission_denied happens on Windows when we try to open a file
        // that has been marked for deletion.
        if (EC == errc::file_exists || EC == errc::permission_denied)
          continue;
        return EC;
      }

      return std::error_code();
    }

    case FS_Name: {
      EC = sys::fs::access(ResultPath.begin(), sys::fs::AccessMode::Exist);
      if (EC == errc::no_such_file_or_directory)
        return std::error_code();
      if (EC)
        return EC;
      continue;
    }

    case FS_Dir: {
      EC = sys::fs::create_directory(ResultPath.begin(), false);
      if (EC) {
        if (EC == errc::file_exists)
          continue;
        return EC;
      }
      return std::error_code();
    }
    }
    exi_unreachable("Invalid Type");
  }
  return EC;
}

namespace exi {
namespace sys  {
namespace path {

const_iterator begin(StrRef path, Style style) {
  const_iterator i;
  i.Path      = path;
  i.Component = find_first_component(path, style);
  i.Position  = 0;
  i.S = style;
  return i;
}

const_iterator end(StrRef path) {
  const_iterator i;
  i.Path      = path;
  i.Position  = path.size();
  return i;
}

const_iterator &const_iterator::operator++() {
  exi_assert(Position < Path.size(), "Tried to increment past end!");

  // Increment Position to past the current component
  Position += Component.size();

  // Check for end.
  if (Position == Path.size()) {
    Component = StrRef();
    return *this;
  }

  // Both POSIX and Windows treat paths that begin with exactly two separators
  // specially.
  bool was_net = Component.size() > 2 && is_separator(Component[0], S) &&
                 Component[1] == Component[0] && !is_separator(Component[2], S);

  // Handle separators.
  if (is_separator(Path[Position], S)) {
    // Root dir.
    if (was_net ||
        // c:/
        (is_style_windows(S) && Component.ends_with(":"))) {
      Component = Path.substr(Position, 1);
      return *this;
    }

    // Skip extra separators.
    while (Position != Path.size() && is_separator(Path[Position], S)) {
      ++Position;
    }

    // Treat trailing '/' as a '.', unless it is the root dir.
    if (Position == Path.size() && Component != "/") {
      --Position;
      Component = ".";
      return *this;
    }
  }

  // Find next component.
  usize end_pos = Path.find_first_of(separators(S), Position);
  Component = Path.slice(Position, end_pos);

  return *this;
}

bool const_iterator::operator==(const const_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() && Position == RHS.Position;
}

ptrdiff_t const_iterator::operator-(const const_iterator &RHS) const {
  return Position - RHS.Position;
}

reverse_iterator rbegin(StrRef Path, Style style) {
  reverse_iterator I;
  I.Path = Path;
  I.Position = Path.size();
  I.S = style;
  ++I;
  return I;
}

reverse_iterator rend(StrRef Path) {
  reverse_iterator I;
  I.Path = Path;
  I.Component = Path.substr(0, 0);
  I.Position = 0;
  return I;
}

reverse_iterator &reverse_iterator::operator++() {
  usize root_dir_pos = root_dir_start(Path, S);

  // Skip separators unless it's the root directory.
  usize end_pos = Position;
  while (end_pos > 0 && (end_pos - 1) != root_dir_pos &&
         is_separator(Path[end_pos - 1], S))
    --end_pos;

  // Treat trailing '/' as a '.', unless it is the root dir.
  if (Position == Path.size() && !Path.empty() &&
      is_separator(Path.back(), S) &&
      (root_dir_pos == StrRef::npos || end_pos - 1 > root_dir_pos)) {
    --Position;
    Component = ".";
    return *this;
  }

  // Find next separator.
  usize start_pos = filename_pos(Path.substr(0, end_pos), S);
  Component = Path.slice(start_pos, end_pos);
  Position = start_pos;
  return *this;
}

bool reverse_iterator::operator==(const reverse_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() && Component == RHS.Component &&
         Position == RHS.Position;
}

ptrdiff_t reverse_iterator::operator-(const reverse_iterator &RHS) const {
  return Position - RHS.Position;
}

StrRef root_path(StrRef path, Style style) {
  const_iterator b = begin(path, style), pos = b, e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = is_style_windows(style) && b->ends_with(":");

    if (has_net || has_drive) {
      if ((++pos != e) && is_separator((*pos)[0], style)) {
        // {C:/,//net/}, so get the first two components.
        return path.substr(0, b->size() + pos->size());
      }
      // just {C:,//net}, return the first component.
      return *b;
    }

    // POSIX style root directory.
    if (is_separator((*b)[0], style)) {
      return *b;
    }
  }

  return StrRef();
}

StrRef root_name(StrRef path, Style style) {
  const_iterator b = begin(path, style), e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = is_style_windows(style) && b->ends_with(":");

    if (has_net || has_drive) {
      // just {C:,//net}, return the first component.
      return *b;
    }
  }

  // No path or no name.
  return StrRef();
}

StrRef root_directory(StrRef path, Style style) {
  const_iterator b = begin(path, style), pos = b, e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = is_style_windows(style) && b->ends_with(":");

    if ((has_net || has_drive) &&
        // {C:,//net}, skip to the next component.
        (++pos != e) && is_separator((*pos)[0], style)) {
      return *pos;
    }

    // POSIX style root directory.
    if (!has_net && is_separator((*b)[0], style)) {
      return *b;
    }
  }

  // No path or no root.
  return StrRef();
}

StrRef relative_path(StrRef path, Style style) {
  StrRef root = root_path(path, style);
  return path.substr(root.size());
}

void append(SmallVecImpl<char> &path, Style style, const Twine &a,
            const Twine &b, const Twine &c, const Twine &d) {
  SmallStr<32> a_storage;
  SmallStr<32> b_storage;
  SmallStr<32> c_storage;
  SmallStr<32> d_storage;

  SmallVec<StrRef, 4> components;
  if (!a.isTriviallyEmpty()) components.push_back(a.toStrRef(a_storage));
  if (!b.isTriviallyEmpty()) components.push_back(b.toStrRef(b_storage));
  if (!c.isTriviallyEmpty()) components.push_back(c.toStrRef(c_storage));
  if (!d.isTriviallyEmpty()) components.push_back(d.toStrRef(d_storage));

  for (auto &component : components) {
    bool path_has_sep =
        !path.empty() && is_separator(path[path.size() - 1], style);
    if (path_has_sep) {
      // Strip separators from beginning of component.
      usize loc = component.find_first_not_of(separators(style));
      StrRef c = component.substr(loc);

      // Append it.
      path.append(c.begin(), c.end());
      continue;
    }

    bool component_has_sep =
        !component.empty() && is_separator(component[0], style);
    if (!component_has_sep &&
        !(path.empty() || has_root_name(component, style))) {
      // Add a separator.
      path.push_back(preferred_separator(style));
    }

    path.append(component.begin(), component.end());
  }
}

void append(SmallVecImpl<char> &path, const Twine &a, const Twine &b,
            const Twine &c, const Twine &d) {
  append(path, Style::native, a, b, c, d);
}

void append(SmallVecImpl<char> &path, const_iterator begin,
            const_iterator end, Style style) {
  for (; begin != end; ++begin)
    path::append(path, style, *begin);
}

StrRef parent_path(StrRef path, Style style) {
  usize end_pos = parent_path_end(path, style);
  if (end_pos == StrRef::npos)
    return StrRef();
  return path.substr(0, end_pos);
}

void remove_filename(SmallVecImpl<char> &path, Style style) {
  usize end_pos = parent_path_end(StrRef(path.begin(), path.size()), style);
  if (end_pos != StrRef::npos)
    path.truncate(end_pos);
}

void replace_extension(SmallVecImpl<char> &path, const Twine &extension,
                       Style style) {
  StrRef p(path.begin(), path.size());
  SmallStr<32> ext_storage;
  StrRef ext = extension.toStrRef(ext_storage);

  // Erase existing extension.
  usize pos = p.find_last_of('.');
  if (pos != StrRef::npos && pos >= filename_pos(p, style))
    path.truncate(pos);

  // Append '.' if needed.
  if (ext.size() > 0 && ext[0] != '.')
    path.push_back('.');

  // Append extension.
  path.append(ext.begin(), ext.end());
}

static bool starts_with(StrRef Path, StrRef Prefix,
                        Style style = Style::native) {
  // Windows prefix matching : case and separator insensitive
  if (is_style_windows(style)) {
    if (Path.size() < Prefix.size())
      return false;
    for (usize I = 0, E = Prefix.size(); I != E; ++I) {
      bool SepPath = is_separator(Path[I], style);
      bool SepPrefix = is_separator(Prefix[I], style);
      if (SepPath != SepPrefix)
        return false;
      if (!SepPath && toLower(Path[I]) != toLower(Prefix[I]))
        return false;
    }
    return true;
  }
  return Path.starts_with(Prefix);
}

bool replace_path_prefix(SmallVecImpl<char> &Path, StrRef OldPrefix,
                         StrRef NewPrefix, Style style) {
  if (OldPrefix.empty() && NewPrefix.empty())
    return false;

  StrRef OrigPath(Path.begin(), Path.size());
  if (!starts_with(OrigPath, OldPrefix, style))
    return false;

  // If prefixes have the same size we can simply copy the new one over.
  if (OldPrefix.size() == NewPrefix.size()) {
    exi::copy(NewPrefix, Path.begin());
    return true;
  }

  StrRef RelPath = OrigPath.substr(OldPrefix.size());
  SmallStr<256> NewPath;
  (Twine(NewPrefix) + RelPath).toVector(NewPath);
  Path.swap(NewPath);
  return true;
}

void native(const Twine &path, SmallVecImpl<char> &result, Style style) {
  exi_assert((!path.isSingleStrRef() ||
          path.getSingleStrRef().data() != result.data()),
         "path and result are not allowed to overlap!");
  // Clear result.
  result.clear();
  path.toVector(result);
  native(result, style);
}

void native(SmallVecImpl<char> &Path, Style style) {
  if (Path.empty())
    return;
  if (is_style_windows(style)) {
    for (char &Ch : Path)
      if (is_separator(Ch, style))
        Ch = preferred_separator(style);
    if (Path[0] == '~' && (Path.size() == 1 || is_separator(Path[1], style))) {
      SmallStr<128> PathHome;
      home_directory(PathHome);
      PathHome.append(Path.begin() + 1, Path.end());
      Path = PathHome;
    }
  } else {
    std::replace(Path.begin(), Path.end(), '\\', '/');
  }
}

String convert_to_slash(StrRef path, Style style) {
  if (is_style_posix(style))
    return String(path);

  String s = path.str();
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

StrRef filename(StrRef path, Style style) { return *rbegin(path, style); }

StrRef stem(StrRef path, Style style) {
  StrRef fname = filename(path, style);
  usize pos = fname.find_last_of('.');
  if (pos == StrRef::npos)
    return fname;
  if ((fname.size() == 1 && fname == ".") ||
      (fname.size() == 2 && fname == ".."))
    return fname;
  return fname.substr(0, pos);
}

StrRef extension(StrRef path, Style style) {
  StrRef fname = filename(path, style);
  usize pos = fname.find_last_of('.');
  if (pos == StrRef::npos)
    return StrRef();
  if ((fname.size() == 1 && fname == ".") ||
      (fname.size() == 2 && fname == ".."))
    return StrRef();
  return fname.substr(pos);
}

bool is_separator(char value, Style style) {
  if (value == '/')
    return true;
  if (is_style_windows(style))
    return value == '\\';
  return false;
}

StrRef get_separator(Style style) {
  if (real_style(style) == Style::windows)
    return "\\";
  return "/";
}

bool has_root_name(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !root_name(p, style).empty();
}

bool has_root_directory(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !root_directory(p, style).empty();
}

bool has_root_path(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !root_path(p, style).empty();
}

bool has_relative_path(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !relative_path(p, style).empty();
}

bool has_filename(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !filename(p, style).empty();
}

bool has_parent_path(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !parent_path(p, style).empty();
}

bool has_stem(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !stem(p, style).empty();
}

bool has_extension(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  return !extension(p, style).empty();
}

bool is_absolute(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  bool rootDir = has_root_directory(p, style);
  bool rootName = is_style_posix(style) || has_root_name(p, style);

  return rootDir && rootName;
}

bool is_absolute_gnu(const Twine &path, Style style) {
  SmallStr<128> path_storage;
  StrRef p = path.toStrRef(path_storage);

  // Handle '/' which is absolute for both Windows and POSIX systems.
  // Handle '\\' on Windows.
  if (!p.empty() && is_separator(p.front(), style))
    return true;

  if (is_style_windows(style)) {
    // Handle drive letter pattern (a character followed by ':') on Windows.
    if (p.size() >= 2 && (p[0] && p[1] == ':'))
      return true;
  }

  return false;
}

bool is_relative(const Twine &path, Style style) {
  return !is_absolute(path, style);
}

StrRef remove_leading_dotslash(StrRef Path, Style style) {
  // Remove leading "./" (or ".//" or "././" etc.)
  while (Path.size() > 2 && Path[0] == '.' && is_separator(Path[1], style)) {
    Path = Path.substr(2);
    while (Path.size() > 0 && is_separator(Path[0], style))
      Path = Path.substr(1);
  }
  return Path;
}

// Remove path traversal components ("." and "..") when possible, and
// canonicalize slashes.
bool remove_dots(SmallVecImpl<char> &the_path, bool remove_dot_dot,
                 Style style) {
  style = real_style(style);
  StrRef remaining(the_path.data(), the_path.size());
  bool needs_change = false;
  SmallVec<StrRef, 16> components;

  // Consume the root path, if present.
  StrRef root = path::root_path(remaining, style);
  bool absolute = !root.empty();
  if (absolute)
    remaining = remaining.drop_front(root.size());

  // Loop over path components manually. This makes it easier to detect
  // non-preferred slashes and double separators that must be canonicalized.
  while (!remaining.empty()) {
    usize next_slash = remaining.find_first_of(separators(style));
    if (next_slash == StrRef::npos)
      next_slash = remaining.size();
    StrRef component = remaining.take_front(next_slash);
    remaining = remaining.drop_front(next_slash);

    // Eat the slash, and check if it is the preferred separator.
    if (!remaining.empty()) {
      needs_change |= remaining.front() != preferred_separator(style);
      remaining = remaining.drop_front();
      // The path needs to be rewritten if it has a trailing slash.
      // FIXME: This is emergent behavior that could be removed.
      needs_change |= remaining.empty();
    }

    // Check for path traversal components or double separators.
    if (component.empty() || component == ".") {
      needs_change = true;
    } else if (remove_dot_dot && component == "..") {
      needs_change = true;
      // Do not allow ".." to remove the root component. If this is the
      // beginning of a relative path, keep the ".." component.
      if (!components.empty() && components.back() != "..") {
        components.pop_back();
      } else if (!absolute) {
        components.push_back(component);
      }
    } else {
      components.push_back(component);
    }
  }

  SmallStr<256> buffer = root;
  // "root" could be "/", which may need to be translated into "\".
  make_preferred(buffer, style);
  needs_change |= root != buffer;

  // Avoid rewriting the path unless we have to.
  if (!needs_change)
    return false;

  if (!components.empty()) {
    buffer += components[0];
    for (StrRef C : ArrayRef(components).drop_front()) {
      buffer += preferred_separator(style);
      buffer += C;
    }
  }
  the_path.swap(buffer);
  return true;
}

} // namespace path

namespace fs {

std::error_code getUniqueID(const Twine Path, UniqueID &Result) {
  file_status Status;
  std::error_code EC = status(Path, Status);
  if (EC)
    return EC;
  Result = Status.getUniqueID();
  return std::error_code();
}

void createUniquePath(const Twine &Model, SmallVecImpl<char> &ResultPath,
                      bool MakeAbsolute) {
  SmallStr<128> ModelStorage;
  Model.toVector(ModelStorage);

  if (MakeAbsolute) {
    // Make model absolute by prepending a temp directory if it's not already.
    if (!sys::path::is_absolute(Twine(ModelStorage))) {
      SmallStr<128> TDir;
      sys::path::system_temp_directory(true, TDir);
      sys::path::append(TDir, Twine(ModelStorage));
      ModelStorage.swap(TDir);
    }
  }

  ResultPath = ModelStorage;
  ResultPath.push_back(0);
  ResultPath.pop_back();

  // Replace '%' with random chars.
  for (unsigned i = 0, e = ModelStorage.size(); i != e; ++i) {
    if (ModelStorage[i] == '%')
      ResultPath[i] = "0123456789abcdef"[sys::Process::GetRandomNumber() & 15];
  }
}

std::error_code createUniqueFile(const Twine &Model, int &ResultFd,
                                 SmallVecImpl<char> &ResultPath,
                                 OpenFlags Flags, unsigned Mode) {
  return createUniqueEntity(Model, ResultFd, ResultPath, false, FS_File, Flags,
                            Mode);
}

std::error_code createUniqueFile(const Twine &Model,
                                 SmallVecImpl<char> &ResultPath,
                                 unsigned Mode) {
  int FD;
  auto EC = createUniqueFile(Model, FD, ResultPath, OF_None, Mode);
  if (EC)
    return EC;
  // FD is only needed to avoid race conditions. Close it right away.
  close(FD);
  return EC;
}

static std::error_code
createTemporaryFile(const Twine &Model, int &ResultFD,
                    exi::SmallVecImpl<char> &ResultPath, FSEntity Type,
                    sys::fs::OpenFlags Flags = sys::fs::OF_None) {
  SmallStr<128> Storage;
  StrRef P = Model.toNullTerminatedStrRef(Storage);
  exi_assert(P.find_first_of(separators(Style::native)) == StrRef::npos,
         "Model must be a simple filename.");
  // Use P.begin() so that createUniqueEntity doesn't need to recreate Storage.
  return createUniqueEntity(P.begin(), ResultFD, ResultPath, true, Type, Flags,
                            all_read | all_write);
}

static std::error_code
createTemporaryFile(const Twine &Prefix, StrRef Suffix, int &ResultFD,
                    exi::SmallVecImpl<char> &ResultPath, FSEntity Type,
                    sys::fs::OpenFlags Flags = sys::fs::OF_None) {
  const char *Middle = Suffix.empty() ? "-%%%%%%" : "-%%%%%%.";
  return createTemporaryFile(Prefix + Middle + Suffix, ResultFD, ResultPath,
                             Type, Flags);
}

std::error_code createTemporaryFile(const Twine &Prefix, StrRef Suffix,
                                    int &ResultFD,
                                    SmallVecImpl<char> &ResultPath,
                                    sys::fs::OpenFlags Flags) {
  return createTemporaryFile(Prefix, Suffix, ResultFD, ResultPath, FS_File,
                             Flags);
}

std::error_code createTemporaryFile(const Twine &Prefix, StrRef Suffix,
                                    SmallVecImpl<char> &ResultPath,
                                    sys::fs::OpenFlags Flags) {
  int FD;
  auto EC = createTemporaryFile(Prefix, Suffix, FD, ResultPath, Flags);
  if (EC)
    return EC;
  // FD is only needed to avoid race conditions. Close it right away.
  close(FD);
  return EC;
}

// This is a mkdtemp with a different pattern. We use createUniqueEntity mostly
// for consistency. We should try using mkdtemp.
std::error_code createUniqueDirectory(const Twine &Prefix,
                                      SmallVecImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Prefix + "-%%%%%%", Dummy, ResultPath, true,
                            FS_Dir);
}

std::error_code
getPotentiallyUniqueFileName(const Twine &Model,
                             SmallVecImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Model, Dummy, ResultPath, false, FS_Name);
}

std::error_code
getPotentiallyUniqueTempFileName(const Twine &Prefix, StrRef Suffix,
                                 SmallVecImpl<char> &ResultPath) {
  int Dummy;
  return createTemporaryFile(Prefix, Suffix, Dummy, ResultPath, FS_Name);
}

void make_absolute(const Twine &current_directory,
                   SmallVecImpl<char> &path) {
  StrRef p(path.data(), path.size());

  bool rootDirectory = path::has_root_directory(p);
  bool rootName = path::has_root_name(p);

  // Already absolute.
  if ((rootName || is_style_posix(Style::native)) && rootDirectory)
    return;

  // All of the following conditions will need the current directory.
  SmallStr<128> current_dir;
  current_directory.toVector(current_dir);

  // Relative path. Prepend the current directory.
  if (!rootName && !rootDirectory) {
    // Append path to the current directory.
    path::append(current_dir, p);
    // Set path to the result.
    path.swap(current_dir);
    return;
  }

  if (!rootName && rootDirectory) {
    StrRef cdrn = path::root_name(current_dir);
    SmallStr<128> curDirRootName(cdrn.begin(), cdrn.end());
    path::append(curDirRootName, p);
    // Set path to the result.
    path.swap(curDirRootName);
    return;
  }

  if (rootName && !rootDirectory) {
    StrRef pRootName      = path::root_name(p);
    StrRef bRootDirectory = path::root_directory(current_dir);
    StrRef bRelativePath  = path::relative_path(current_dir);
    StrRef pRelativePath  = path::relative_path(p);

    SmallStr<128> res;
    path::append(res, pRootName, bRootDirectory, bRelativePath, pRelativePath);
    path.swap(res);
    return;
  }

  exi_unreachable("All rootName and rootDirectory combinations should have "
                   "occurred above!");
}

std::error_code make_absolute(SmallVecImpl<char> &path) {
  if (path::is_absolute(path))
    return {};

  SmallStr<128> current_dir;
  if (std::error_code ec = current_path(current_dir))
    return ec;

  make_absolute(current_dir, path);
  return {};
}

std::error_code create_directories(const Twine &Path, bool IgnoreExisting,
                                   perms Perms) {
  SmallStr<128> PathStorage;
  StrRef P = Path.toStrRef(PathStorage);

  // Be optimistic and try to create the directory
  std::error_code EC = create_directory(P, IgnoreExisting, Perms);
  // If we succeeded, or had any error other than the parent not existing, just
  // return it.
  if (EC != errc::no_such_file_or_directory)
    return EC;

  // We failed because of a no_such_file_or_directory, try to create the
  // parent.
  StrRef Parent = path::parent_path(P);
  if (Parent.empty())
    return EC;

  if ((EC = create_directories(Parent, IgnoreExisting, Perms)))
      return EC;

  return create_directory(P, IgnoreExisting, Perms);
}

static std::error_code copy_file_internal(int ReadFD, int WriteFD) {
  const usize BufSize = 4096;
  char *Buf = new char[BufSize];
  int BytesRead = 0, BytesWritten = 0;
  for (;;) {
    BytesRead = read(ReadFD, Buf, BufSize);
    if (BytesRead <= 0)
      break;
    while (BytesRead) {
      BytesWritten = write(WriteFD, Buf, BytesRead);
      if (BytesWritten < 0)
        break;
      BytesRead -= BytesWritten;
    }
    if (BytesWritten < 0)
      break;
  }
  delete[] Buf;

  if (BytesRead < 0 || BytesWritten < 0)
    return errnoAsErrorCode();
  return std::error_code();
}

#ifndef __APPLE__
std::error_code copy_file(const Twine &From, const Twine &To) {
  int ReadFD, WriteFD;
  if (std::error_code EC = openFileForRead(From, ReadFD, OF_None))
    return EC;
  if (std::error_code EC =
          openFileForWrite(To, WriteFD, CD_CreateAlways, OF_None)) {
    close(ReadFD);
    return EC;
  }

  std::error_code EC = copy_file_internal(ReadFD, WriteFD);

  close(ReadFD);
  close(WriteFD);

  return EC;
}
#endif

std::error_code copy_file(const Twine &From, int ToFD) {
  int ReadFD;
  if (std::error_code EC = openFileForRead(From, ReadFD, OF_None))
    return EC;

  std::error_code EC = copy_file_internal(ReadFD, ToFD);

  close(ReadFD);

  return EC;
}

ErrorOr<MD5::MD5Result> md5_contents(int FD) {
  MD5 Hash;

  constexpr usize BufSize = 4096;
  std::vector<u8> Buf(BufSize);
  int BytesRead = 0;
  for (;;) {
    BytesRead = read(FD, Buf.data(), BufSize);
    if (BytesRead <= 0)
      break;
    Hash.update(ArrayRef(Buf.data(), BytesRead));
  }

  if (BytesRead < 0)
    return errnoAsErrorCode();
  MD5::MD5Result Result;
  Hash.final(Result);
  return Result;
}

ErrorOr<MD5::MD5Result> md5_contents(const Twine &Path) {
  int FD;
  if (auto EC = openFileForRead(Path, FD, OF_None))
    return EC;

  auto Result = md5_contents(FD);
  close(FD);
  return Result;
}

bool exists(const basic_file_status &status) {
  return status_known(status) && status.type() != file_type::file_not_found;
}

bool status_known(const basic_file_status &s) {
  return s.type() != file_type::status_error;
}

file_type get_file_type(const Twine &Path, bool Follow) {
  file_status st;
  if (status(Path, st, Follow))
    return file_type::status_error;
  return st.type();
}

bool is_directory(const basic_file_status &status) {
  return status.type() == file_type::directory_file;
}

std::error_code is_directory(const Twine &path, bool &result) {
  file_status st;
  if (std::error_code ec = status(path, st))
    return ec;
  result = is_directory(st);
  return std::error_code();
}

bool is_regular_file(const basic_file_status &status) {
  return status.type() == file_type::regular_file;
}

std::error_code is_regular_file(const Twine &path, bool &result) {
  file_status st;
  if (std::error_code ec = status(path, st))
    return ec;
  result = is_regular_file(st);
  return std::error_code();
}

bool is_symlink_file(const basic_file_status &status) {
  return status.type() == file_type::symlink_file;
}

std::error_code is_symlink_file(const Twine &path, bool &result) {
  file_status st;
  if (std::error_code ec = status(path, st, false))
    return ec;
  result = is_symlink_file(st);
  return std::error_code();
}

bool is_other(const basic_file_status &status) {
  return exists(status) &&
         !is_regular_file(status) &&
         !is_directory(status);
}

std::error_code is_other(const Twine &Path, bool &Result) {
  file_status FileStatus;
  if (std::error_code EC = status(Path, FileStatus))
    return EC;
  Result = is_other(FileStatus);
  return std::error_code();
}

void directory_entry::replace_filename(const Twine &Filename, file_type Type,
                                       basic_file_status Status) {
  SmallStr<128> PathStr = path::parent_path(Path);
  path::append(PathStr, Filename);
  this->Path = String(PathStr);
  this->Type = Type;
  this->Status = Status;
}

ErrorOr<perms> getPermissions(const Twine &Path) {
  file_status Status;
  if (std::error_code EC = status(Path, Status))
    return EC;

  return Status.permissions();
}

usize mapped_file_region::size() const {
  exi_assert(Mapping, "Mapping failed but used anyway!");
  return Size;
}

char *mapped_file_region::data() const {
  exi_assert(Mapping, "Mapping failed but used anyway!");
  return reinterpret_cast<char *>(Mapping);
}

const char *mapped_file_region::const_data() const {
  exi_assert(Mapping, "Mapping failed but used anyway!");
  return reinterpret_cast<const char *>(Mapping);
}

Error readNativeFileToEOF(file_t FileHandle, SmallVecImpl<char> &Buffer,
                          ssize_t ChunkSize) {
  // Install a handler to truncate the buffer to the correct size on exit.
  usize Size = Buffer.size();
  auto TruncateOnExit = make_scope_exit([&]() { Buffer.truncate(Size); });

  // Read into Buffer until we hit EOF.
  for (;;) {
    Buffer.resize_for_overwrite(Size + ChunkSize);
    Expected<usize> ReadBytes = readNativeFile(
        FileHandle, MutArrayRef(Buffer.begin() + Size, ChunkSize));
    if (!ReadBytes)
      return ReadBytes.takeError();
    if (*ReadBytes == 0)
      return Error::success();
    Size += *ReadBytes;
  }
}

} // namespace fs
} // namespace sys
} // namespace exi

// Include the truly platform-specific parts.
#if EXI_ON_UNIX
# include "Unix/Path.impl"
#endif
#if defined(_WIN32)
# include "Windows/Path.impl"
#endif

namespace exi {
namespace sys {
namespace fs {

TempFile::TempFile(StrRef Name, int FD)
    : TmpName(String(Name)), FD(FD) {}
TempFile::TempFile(TempFile &&Other) { *this = std::move(Other); }
TempFile &TempFile::operator=(TempFile &&Other) {
  TmpName = std::move(Other.TmpName);
  FD = Other.FD;
  Other.Done = true;
  Other.FD = -1;
#ifdef _WIN32
  RemoveOnClose = Other.RemoveOnClose;
  Other.RemoveOnClose = false;
#endif
  return *this;
}

TempFile::~TempFile() { assert(Done); }

Error TempFile::discard() {
  Done = true;
  if (FD != -1 && close(FD) == -1) {
    std::error_code EC = errnoAsErrorCode();
    return errorCodeToError(EC);
  }
  FD = -1;

#ifdef _WIN32
  // On Windows, closing will remove the file, if we set the delete
  // disposition. If not, remove it manually.
  bool Remove = RemoveOnClose;
#else
  // Always try to remove the file.
  bool Remove = true;
#endif
  std::error_code RemoveEC;
  if (Remove && !TmpName.empty()) {
    RemoveEC = fs::remove(TmpName);
    sys::DontRemoveFileOnSignal(TmpName);
    if (!RemoveEC)
      TmpName = "";
  } else {
    TmpName = "";
  }
  return errorCodeToError(RemoveEC);
}

Error TempFile::keep(const Twine &Name) {
  assert(!Done);
  Done = true;
  // Always try to close and rename.
#ifdef _WIN32
  // If we can't cancel the delete don't rename.
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  std::error_code RenameEC =
      RemoveOnClose ? std::error_code() : setDeleteDisposition(H, false);
  bool ShouldDelete = false;
  if (!RenameEC) {
    RenameEC = rename_handle(H, Name);
    // If rename failed because it's cross-device, copy instead
    if (RenameEC ==
      std::error_code(ERROR_NOT_SAME_DEVICE, std::system_category())) {
      RenameEC = copy_file(TmpName, Name);
      ShouldDelete = true;
    }
  }

  // If we can't rename or copy, discard the temporary file.
  if (RenameEC)
    ShouldDelete = true;
  if (ShouldDelete) {
    if (!RemoveOnClose)
      setDeleteDisposition(H, true);
    else
      remove(TmpName);
  }
#else
  std::error_code RenameEC = fs::rename(TmpName, Name);
  if (RenameEC) {
    // If we can't rename, try to copy to work around cross-device link issues.
    RenameEC = sys::fs::copy_file(TmpName, Name);
    // If we can't rename or copy, discard the temporary file.
    if (RenameEC)
      remove(TmpName);
  }
#endif
  sys::DontRemoveFileOnSignal(TmpName);

  if (!RenameEC)
    TmpName = "";

  if (close(FD) == -1)
    return errorCodeToError(errnoAsErrorCode());
  FD = -1;

  return errorCodeToError(RenameEC);
}

Error TempFile::keep() {
  assert(!Done);
  Done = true;

#ifdef _WIN32
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  if (std::error_code EC = setDeleteDisposition(H, false))
    return errorCodeToError(EC);
#endif
  sys::DontRemoveFileOnSignal(TmpName);

  TmpName = "";

  if (close(FD) == -1)
    return errorCodeToError(errnoAsErrorCode());
  FD = -1;

  return Error::success();
}

Expected<TempFile> TempFile::create(const Twine &Model, unsigned Mode,
                                    OpenFlags ExtraFlags) {
  int FD;
  SmallStr<128> ResultPath;
  if (std::error_code EC =
          createUniqueFile(Model, FD, ResultPath, OF_Delete | ExtraFlags, Mode))
    return errorCodeToError(EC);

  TempFile Ret(ResultPath, FD);
#ifdef _WIN32
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  bool SetSignalHandler = false;
  if (std::error_code EC = setDeleteDisposition(H, true)) {
    Ret.RemoveOnClose = true;
    SetSignalHandler = true;
  }
#else
  bool SetSignalHandler = true;
#endif
  if (SetSignalHandler && sys::RemoveFileOnSignal(ResultPath)) {
    // Make sure we delete the file when RemoveFileOnSignal fails.
    consumeError(Ret.discard());
    std::error_code EC(errc::operation_not_permitted);
    return errorCodeToError(EC);
  }
  return std::move(Ret);
}
} // namespace fs

} // namespace sys
} // namespace exi
