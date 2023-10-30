/*
 * Copyright 2016 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4512 4127 4068 4244 4267 4251 4146)
#endif

#include <google/protobuf/descriptor.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/descriptor.pb.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Generator.hh"

namespace google {
namespace protobuf {
namespace compiler {
namespace cpp {

/////////////////////////////////////////////////
void replaceAll(std::string &_src, const std::string &_oldValue,
  const std::string &_newValue)
{
    for (size_t i = 0; (i = _src.find(_oldValue, i)) != std::string::npos;)
    {
      _src.replace(i, _oldValue.length(), _newValue);
      i += _newValue.length() - _oldValue.length() + 1;
    }
}

/////////////////////////////////////////////////
std::vector<std::string> getNamespaces(const std::string &_package)
{
  std::vector<std::string> result;
  std::stringstream ss(_package);
  std::string item;
  while (getline (ss, item, '.')) {
    result.push_back (item);
  }
  return result;
}


/////////////////////////////////////////////////
Generator::Generator(const std::string &/*_name*/)
{
}

/////////////////////////////////////////////////
Generator::~Generator() = default;

/////////////////////////////////////////////////
bool Generator::Generate(const FileDescriptor *_file,
                               const std::string &/*_parameter*/,
                               OutputDirectory *_generatorContext,
                               std::string * /*_error*/) const
{
  auto filePath = std::filesystem::path(_file->name());
  auto parent_path = filePath.parent_path();
  auto fileStem = filePath.stem().string();

  // protoc generates gz/msgs/msg.pb.cc and gz/msgs/msg.pb.hh
  // This generator injects code into the msg.pb.cc file, but generates
  // a completely new header that wraps the original protobuf header
  //
  // The renaming operation is handled via the outside python script
  //
  // gz/msgs/msg.pb.cc - stays in place
  // gz/msgs/msg.pb.h -> gz/msgs/details/msg.pb.h
  // gz/msgs/msg.gz.h (generated here) -> gz/msgs/msg.pb.h
  std::string identifier;
  std::string headerFilename;
  std::string newHeaderFilename;
  std::string sourceFilename;

  // Can't use pathJoin because protoc always expects forward slashes
  // regardless of platform
  for (auto part : parent_path)
  {
    identifier += part.string() + "_";
    headerFilename += part.string() + "/";
    newHeaderFilename += part.string() + "/";
    sourceFilename += part.string() + "/";
  }

  std::unique_ptr<io::ZeroCopyOutputStream> message_type_index(
    _generatorContext->Open(identifier + fileStem + ".pb_index"));
  io::Printer indexPrinter(message_type_index.get(), '$');

  identifier += fileStem;
  headerFilename += fileStem + ".gz.h";
  newHeaderFilename += "details/" + fileStem + ".pb.h";
  sourceFilename += fileStem + ".pb.cc";

  std::map<std::string, std::string> variables;
  variables["filename"] = _file->name();
  variables["define_guard"] = identifier;
  variables["detail_header"] = newHeaderFilename;

  {
    std::unique_ptr<io::ZeroCopyOutputStream> output(
        _generatorContext->Open(headerFilename));
    io::Printer printer(output.get(), '$');
    // Generate top of header.
    printer.Print(variables, R"(
// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: $filename$

#ifndef $define_guard$
#define $define_guard$

#include <memory>

#include <gz/msgs/Export.hh>

#include <$detail_header$>
)");

    auto ns = getNamespaces(_file->package());

    for (const auto &name : ns)
        printer.PrintRaw("namespace " + name + " {\n");

    for (auto i = 0; i < _file->message_type_count(); ++i)
    {
      auto desc = _file->message_type(i);
      std::string ptrTypes;

      indexPrinter.PrintRaw(desc->name());
      indexPrinter.PrintRaw("\n");

      // Define std::unique_ptr types for our messages
      ptrTypes += "typedef std::unique_ptr<"
        + desc->name() + "> "
        + desc->name() + "UniquePtr;\n";

      // Define const std::unique_ptr types for our messages
      ptrTypes += "typedef std::unique_ptr<const "
        + desc->name() + "> Const"
        + desc->name() + "UniquePtr;\n";

      // Define std::shared_ptr types for our messages
      ptrTypes += "typedef std::shared_ptr<"
        + desc->name() + "> "
        + desc->name() + "SharedPtr;\n";

      // Define const std::shared_ptr types for our messages
      ptrTypes += "typedef std::shared_ptr<const "
        + desc->name() + "> Const"
        + desc->name() + "SharedPtr;\n";

      printer.PrintRaw(ptrTypes.c_str());
    }

    for (auto name = ns.rbegin(); name != ns.rend(); name++)
    {
      printer.PrintRaw("}  // namespace " + *name + "\n");
    }

    printer.PrintRaw("\n");

    printer.Print(variables, "#endif  // $define_guard$\n");
  }

  return true;
}
}
}
}
}
