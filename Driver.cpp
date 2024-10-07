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

#include <exicpp/Reader.hpp>
#include <exicpp/Writer.hpp>
#include <exicpp/XML.hpp>
#include <exicpp/Debug/Format.hpp>
// #include <exip/EXISerializer.h>

// #define RAPIDXML_NO_STREAMS
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <rapidxml_print.hpp>
#include <fmt/color.h>

#define COLOR_PRINT_(col, fstr, ...) \
  fmt::print(fstr, fmt::styled( \
    fmt::format(__VA_ARGS__), fmt::fg(col)))
#define COLOR_PRINT(col, ...) COLOR_PRINT_(col, "{}", __VA_ARGS__)
#define COLOR_PRINTLN(col, ...) COLOR_PRINT_(col, "{}\n", __VA_ARGS__)

namespace fs = std::filesystem;

struct InternRef : public exi::StrRef {
  using BaseType = exi::StrRef;
public:
  constexpr InternRef() noexcept = default;
  InternRef(exi::Char* data, std::size_t len) noexcept : BaseType(data, len) { }
  constexpr InternRef(const InternRef&) noexcept = default;
  constexpr InternRef& operator=(const InternRef&) noexcept = default;
public:
  exi::Char* data() const { return const_cast<exi::Char*>(BaseType::data()); }
};

struct XMLBuilder {
  using Ch = exi::Char;
  using StrRef = exi::StrRef;
  using ErrCode = exi::ErrCode;
  using Ty = exi::XMLType;

  XMLBuilder() :
    doc(std::make_unique<exi::XMLDocument>()),
    node(doc->document()) {
  }

  void dump() {
    std::cout << *doc << std::endl;
  }

public:
  ErrCode startDocument() {
    this->node = doc->document();
    LOG_ASSERT(node && node->type() == Ty::node_document);
    return ErrCode::Ok;
  }

  ErrCode endDocument() {
    LOG_ASSERT(node && node->type() == Ty::node_document);
    return ErrCode::Ok;
  }

  ErrCode startElement(const exi::QName& name) {
    const auto ln = this->internQName(name);
    auto ty = ln.empty() ? Ty::node_data : Ty::node_element;
    auto* newNode = this->makeNode<true>(ty, ln);
    // Add as child
    node->append_node(newNode);
    this->node = newNode;
    return ErrCode::Ok;
  }

  ErrCode endElement() {
    LOG_ASSERT(node);
    this->node = node->parent();
    return ErrCode::Ok;
  }

  ErrCode namespaceDeclaration(
    exi::StrRef ns,
    exi::StrRef prefix,
    bool isLocal) 
  {
    if (isLocal && !prefix.empty()) {
      StrRef name(node->name(), node->name_size());
      auto fullName = fmt::format("{}:{}", prefix, name);
      InternRef iname = this->intern(fullName);
      node->name(iname.data(), iname.size());
    }

    auto fullPre = XMLBuilder::FormatNs(prefix);
    auto* attr = this->makeAttribute(fullPre, ns);
    node->append_attribute(attr);
    return ErrCode::Ok;
  }

  ErrCode attribute(const exi::QName& name) {
    LOG_ASSERT(!this->attr);
    this->attr = this->makeAttribute(name.localName());
    node->append_attribute(attr);
    return ErrCode::Ok;
  }

  ErrCode stringData(exi::StrRef str) {
    if (this->attr) {
      InternRef istr = this->intern(str);
      attr->value(istr.data(), istr.size());
      this->attr = nullptr;
      return ErrCode::Ok;
    }

    LOG_ASSERT(node->type() == Ty::node_data);
    InternRef istr = this->intern(str);
    node->value(istr.data(), istr.size());
    return ErrCode::Ok;
  }

private:
  static std::string FormatNs(StrRef prefix) {
    if (prefix.empty())
      return "xmlns";
    return fmt::format("xmlns:{}", prefix);
  }

  InternRef internQName(const exi::QName& qname) {
    const auto prefix = qname.prefix();
    if (prefix.empty()) {
      return this->intern(qname.localName());
    }
    
    auto fullName = fmt::format("{}:{}", prefix, qname.localName());
    return this->intern(fullName);
  }

  template <bool AssureInterned = false>
  InternRef intern(StrRef str) {
    if (str.empty())
      return {nullptr, 0};
    if constexpr (AssureInterned) {
      auto* raw = const_cast<Ch*>(str.data());
      return {raw, str.size()};
    }
    auto it = intern_table.find(str);
    if (it != intern_table.end())
      return it->second;
    return this->makePooledStr(str);
  }

  InternRef makePooledStr(StrRef str) {
    if (str.empty())
      return {nullptr, 0};
    const std::size_t len = str.size();
    Ch* rawStr = doc->allocate_string(nullptr, len);
    std::char_traits<Ch>::copy(rawStr, str.data(), len);

    InternRef is {rawStr, len};
    LOG_ASSERT(intern_table.count(str) == 0);
    intern_table[str] = is;
    return is;
  }

  template <bool AssureInterned = false>
  exi::XMLNode* makeNode(Ty type, StrRef name = "", StrRef value = "") {
    auto iname = this->intern<AssureInterned>(name);
    auto ivalue = this->intern<AssureInterned>(value);
    return doc->allocate_node(type,
      iname.data(), ivalue.data(),
      iname.size(), ivalue.size()
    );
  }

  template <bool AssureInterned = false>
  exi::XMLAttribute* makeAttribute(StrRef name, StrRef value = "") {
    auto iname = this->intern<AssureInterned>(name);
    auto ivalue = this->intern<AssureInterned>(value);
    return doc->allocate_attribute(
      iname.data(), ivalue.data(),
      iname.size(), ivalue.size()
    );
  }

private:
  exi::Box<exi::XMLDocument> doc;
  exi::XMLNode* node = nullptr;
  exi::XMLAttribute* attr = nullptr;
  std::unordered_map<StrRef, InternRef> intern_table;
};

std::string get_relative(exi::StrRef path);
bool write_file(const std::string& path, const std::string& outpath);
bool read_file(const std::string& outpath);
void test_file(exi::StrRef filepath);
void test_exi(exi::StrRef file, bool printSep = true);

void write_file_test(exi::StrRef filepath, bool debugMode = true) {
  fmt::print(fmt::fg(fmt::color::blue_violet),
    "\n|=[ {} ]===========================================|\n", filepath);
  const auto basepath = get_relative(filepath);
  std::string path = basepath + ".xml";
  std::string outpath = basepath + ".exi";
  
  const bool oldval = DEBUG_GET_MODE();
  DEBUG_SET_MODE(debugMode);
  write_file(path, outpath);
  DEBUG_SET_MODE(oldval);
}

int main(int argc, char* argv[]) {
  // test_exi("vendored/exip/examples/simpleDecoding/exipd-test.exi");
  // test_file("examples/Basic2");
  // test_file("examples/Basic");
  test_file("examples/Namespace");
  // write_file_test("examples/Namespace", false);
  // test_exi("examples/NamespaceG.exi");
  // DEBUG_SET_MODE(OFF);
  // test_exi("examples/Namespace.exi");
  // test_exi("examples/nspreserve.exi");
  // test_file("examples/PersonnelA");
  // test_exi("examples/PersonnelB.exi");
  // test_exi("examples/notebook.xml.exi");
  // test_exi("examples/XMLSample.exi");
}

bool write_file(const std::string& path, const std::string& outpath) {
  using namespace exi;
  auto xmldoc = BoundDocument::ParseFrom(path);
  if (!xmldoc) {
    COLOR_PRINTLN(fmt::color::red,
      "Unable to locate file '{}'!", path);
    return false;
  }

  InlineStackBuffer<512> buf;
  if (Error E = buf.writeFile(outpath)) {
    COLOR_PRINTLN(fmt::color::red,
      "Error in '{}': {}", outpath, E.message());
    return false;
  }

  if (Error E = write_xml(xmldoc.document(), buf)) {
    // std::cout 
    //   << ansi::red << "Serialization error: " << E.message()
    //   << ansi::reset << std::endl;
    return false;
  }

  return true;
}

bool read_file(const std::string& outpath) {
  using namespace exi;

  InlineStackBuffer<512> buf;
  if (Error E = buf.readFile(outpath)) {
    COLOR_PRINTLN(fmt::color::red,
      "Error in '{}': {}", outpath, E.message());
    return false;
  }

  XMLBuilder builder {};
  auto parser = exi::Parser::New(builder, buf);

  if (Error E = parser.parseHeader()) {
    COLOR_PRINTLN(fmt::color::red,
      "\nError in '{}'\n", outpath);
    return false;
  }

  if (Error E = parser.parseAll()) {
    COLOR_PRINTLN(fmt::color::red,
      "\nError in '{}'\n", outpath);
    return false;
  }

  builder.dump();
  return true;
}

exi::StrRef file_folder() {
  constexpr auto& rawfile = __FILE__;
  constexpr exi::StrRef file {rawfile};
  constexpr std::size_t pos = file.find_last_of("\\/");
  if (pos == exi::StrRef::npos)
    return "";
  return file.substr(0, pos + 1);
}

std::string get_relative(exi::StrRef path) {
  return std::string(file_folder()) + std::string(path);
}

void test_file(exi::StrRef filepath) {
  fmt::print(fmt::fg(fmt::color::blue_violet),
    "\n|=[ {} ]===========================================|\n", filepath);
  const auto basepath = get_relative(filepath);
  std::string path = basepath + ".xml";
  std::string outpath = basepath + ".exi";

  const bool oldval = DEBUG_GET_MODE();
  DEBUG_SET_MODE(ON);
  if (!write_file(path, outpath)) {
    DEBUG_SET_MODE(oldval);
    return;
  }
  fmt::print(fmt::fg(fmt::color::blue_violet),
    "\n----------------------------------------------\n");
  DEBUG_SET_MODE(OFF);
  // test_exi(std::string(filepath) + ".exi", false);
  read_file(outpath);
  DEBUG_SET_MODE(oldval);
}
