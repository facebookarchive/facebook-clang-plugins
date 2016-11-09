/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */


#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <iostream>

#include "SimplePluginASTAction.h"

using namespace clang;

namespace {

class PrintFilesConsumer : public ASTConsumer {

 public:
  PrintFilesConsumer(
      const CompilerInstance &CI,
      std::unique_ptr<ASTPluginLib::PluginASTOptionsBase> &&Options,
      std::unique_ptr<std::string> outputFile) {
    std::cout << "Input file: "
              << Options->normalizeSourcePath(Options->inputFile.getFile().str().c_str())
              << "\n";
    std::cout << "Input file kind: " << Options->inputFile.getKind() << "\n";
    if (outputFile) {
      std::cout << "Output file: " << *outputFile << "\n";
    }
  }

};

typedef ASTPluginLib::NoOpenSimplePluginASTAction<PrintFilesConsumer>
    PrintFiles;

}

static FrontendPluginRegistry::Add<PrintFiles> X(
    "PrintFiles", "Print source files and their kinds");
