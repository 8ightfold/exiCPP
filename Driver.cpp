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

#include <Common/SmallStr.hpp>
#include <Common/IntrusiveRefCntPtr.hpp>
#include <Common/PointerIntPair.hpp>
#include <Support/Filesystem.hpp>
#include <Support/Logging.hpp>
#include <Support/MemoryBuffer.hpp>
#include <Support/MemoryBufferRef.hpp>
#include <Support/Process.hpp>
#include <Support/ScopedSave.hpp>
#include <Support/raw_ostream.hpp>
#include <rapidxml.hpp>

#define DEBUG_TYPE "__DRIVER__"

using namespace exi;

using XMLDocument  = xml::XMLDocument<Char>;
using XMLAttribute = xml::XMLAttribute<Char>;
using XMLBase      = xml::XMLBase<Char>;
using XMLNode      = xml::XMLNode<Char>;
using xml::NodeKind;

enum NodeDataKind {
  NDK_None    = 0b000,
  NDK_Nest    = 0b001,
  NDK_Unnest  = 0b010,
};

using EmbeddedNode = PointerIntPair<XMLNode*, 3, NodeDataKind>;

namespace {
class XMLErrorInfo : public ErrorInfo<XMLErrorInfo> {
  friend class ErrorInfo<XMLErrorInfo>;
  static char ID;

  String Msg;
  usize Offset = 0;
  std::error_code EC;
public:
  XMLErrorInfo(const Twine& Msg,
               usize Offset = usize(-1)) :
   XMLErrorInfo(std::errc::illegal_byte_sequence, Msg, Offset) {}
  
  XMLErrorInfo(std::error_code EC, const Twine& Msg,
               usize Offset = usize(-1)) :
   Msg(Msg.str()), Offset(Offset), EC(EC) {}
  
  XMLErrorInfo(std::errc Errc, const Twine& Msg,
               usize Offset = usize(-1)) :
   XMLErrorInfo(std::make_error_code(Errc), Msg, Offset) {}

  void log(raw_ostream& OS) const override {
    OS << "XML Error";
    if (Offset != usize(-1))
      OS << " at " << Offset;
    if (!Msg.empty())
      OS << ": " << Msg;
  }

  std::error_code convertToErrorCode() const override { return EC; }
};

char XMLErrorInfo::ID = 0;
} // namespace `anonymous`

static Box<XMLDocument> CreateXMLDoc(xml::XMLBumpAllocator* Alloc) {
  if (Alloc == nullptr)
    return std::make_unique<XMLDocument>();
  else
    return std::make_unique<XMLDocument>(*Alloc);
}

static Expected<Box<XMLDocument>>
 ParseXMLFromMemoryBuffer(WritableMemoryBuffer& MB,
                          xml::XMLBumpAllocator* Alloc = nullptr) {
  ScopedSave S(xml::use_exceptions_anyway, true);
  outs() << "Reading file \'" << MB.getBufferIdentifier() << "\'\n";
  try {
    static constexpr int ParseRules
      = xml::parse_declaration_node | xml::parse_all;
    auto Doc = CreateXMLDoc(Alloc);
    exi_assert(Doc.get() != nullptr);
    Doc->parse<ParseRules>(MB.getBufferStart());
    return std::move(Doc);
  } catch (const std::exception& Ex) {
#if !RAPIDXML_NO_EXCEPTIONS
    LOG_ERROR("Failed to read file '{}'", MB.getBufferIdentifier());
    /// Check if it's rapidxml's wee type
    if (auto* PEx = dynamic_cast<const xml::parse_error*>(&Ex)) {
      LOG_EXTRA("Error type is 'xml::parse_error'");
      const usize Off = MB.getBufferOffset(PEx->where<Char>());
      return make_error<XMLErrorInfo>(PEx->what(), Off);
    }
#endif
    return make_error<XMLErrorInfo>(Ex.what());
  }
}

Box<WritableMemoryBuffer> getFromPath(
    ExitOnError& OnErrHandler, const Twine& Path) {
  SmallStr<80> UsePath;
  Path.toVector(UsePath);
  sys::fs::make_absolute(UsePath);

  return OnErrHandler(
    errorOrToExpected(WritableMemoryBuffer::getFileEx(
      UsePath, true, false, /*UTF32*/ Align::Constant<4>())));
}

int tests_main(int Argc, char* Argv[]);
int main(int Argc, char* Argv[]) {
  exi::DebugFlag = LogLevel::WARN;
  outs().enable_colors(true);
  dbgs().enable_colors(true);

  ExitOnError ExitOnErr("exicpp: ");

  Box<WritableMemoryBuffer> MBA = getFromPath(
    ExitOnErr, "examples/Namespace.xml");
  Box<XMLDocument> DocA = ExitOnErr(
    ParseXMLFromMemoryBuffer(*MBA));
  outs() << raw_ostream::BRIGHT_GREEN
    << "Read success!\n" << raw_ostream::RESET;
  
  Box<WritableMemoryBuffer> MBB = getFromPath(
    ExitOnErr, "large-examples/treebank_e.xml");
  Box<XMLDocument> DocB = ExitOnErr(
    ParseXMLFromMemoryBuffer(*MBB));
  outs() << raw_ostream::BRIGHT_GREEN
    << "Read success!\n" << raw_ostream::RESET;

  // tests_main(Argc, Argv);
}

#if RAPIDXML_NO_EXCEPTIONS

/// Handles throwing in different modes.
static void Throw([[maybe_unused]] const char* what,
                  [[maybe_unused]] void* where) EXI_NOEXCEPT {
#if EXI_EXCEPTIONS
# if !RAPIDXML_NO_EXCEPTIONS
  throw xml::parse_error(what, where);
# else
  throw std::runtime_error(what);
# endif
#endif
} 

void xml::parse_error_handler(const char* what, void* where) {
  if (xml::use_exceptions_anyway)
    Throw(what, where);
  errs() << "Uhhhh... " << what << '\n';
  sys::Process::Exit(1);
}

#endif // RAPIDXML_NO_EXCEPTIONS

bool xml::use_exceptions_anyway = false;
