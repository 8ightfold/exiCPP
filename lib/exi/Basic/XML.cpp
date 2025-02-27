//===- exi/Basic/XML.cpp --------------------------------------------===//
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
/// This file defines the interface for XML.
///
//===----------------------------------------------------------------===//

#include <exi/Basic/XML.hpp>
#include <core/Common/SmallStr.hpp>
#include <core/Common/StringSwitch.hpp>
#include <core/Common/Twine.hpp>
#include <core/Support/Path.hpp>
#include <core/Support/Process.hpp>
#include <core/Support/raw_ostream.hpp>
#include <rapidxml.hpp>

using namespace exi;

static XMLKind Classify(StrRef Ext) {
  return StringSwitch<XMLKind>(Ext)
    .EndsWithLower("xml", XMLKind::Document)
    .EndsWithLower("exi", XMLKind::XsdExiSchema)
    .EndsWithLower("xsd", XMLKind::XsdXmlSchema)
    .EndsWithLower("dtd", XMLKind::DTDSchema)
    .Default(XMLKind::Unknown);
}

XMLKind exi::classifyXMLKind(StrRef PathOrExt) {
  return Classify(PathOrExt);
}

static XMLKind ClassifyXMLKindTwine(const Twine& PathOrExt) {
  SmallStr<80> Storage;
  return Classify(PathOrExt.toStrRef(Storage));
}

XMLKind exi::classifyXMLKind(const Twine& PathOrExt) {
  if (PathOrExt.isSingleStrRef())
    return Classify(PathOrExt.getSingleStrRef());
  tail_return ClassifyXMLKindTwine(PathOrExt);
}

//////////////////////////////////////////////////////////////////////////
// parse_error_handler

#ifdef RAPIDXML_NO_EXCEPTIONS

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
  errs() << "xml parse error:" << what << '\n';
  sys::Process::Exit(1); // TODO: No cleanup?
}

#endif // RAPIDXML_NO_EXCEPTIONS

bool xml::use_exceptions_anyway = false;
