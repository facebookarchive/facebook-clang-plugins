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

class PrintFilesPreprocessorHandler : public clang::PPCallbacks {
  clang::SourceManager &SM;
  std::shared_ptr<ASTPluginLib::PluginASTOptionsBase> options;

 public:
  PrintFilesPreprocessorHandler(
      clang::SourceManager &SM,
      std::shared_ptr<ASTPluginLib::PluginASTOptionsBase> options,
      std::shared_ptr<ASTPluginLib::EmptyPreprocessorHandlerData> sharedData)
    : SM(SM), options(options) {}

  void FileChanged(SourceLocation loc, FileChangeReason reason,
                   SrcMgr::CharacteristicKind fileType,
                   FileID prevFID = FileID()) override {
    std::string file, prevFile;
    if (SM.getFilename(loc).data() != nullptr) {
      file = options->normalizeSourcePath(SM.getFilename(loc).data());
    }
    if (prevFID.isValid()) {
      prevFile = SM.getFilename(SM.getLocForStartOfFile(prevFID)).str();
    }
    std::string reasonStr;
    switch (reason) {
    case EnterFile: reasonStr = "EnterFile"; break;
    case ExitFile: reasonStr = "ExitFile"; break;
    case SystemHeaderPragma: reasonStr = "SystemHeaderPragma"; break;
    case RenameFile: reasonStr = "RenameFile"; break;
    }
    std::cout << reasonStr << " ";
    if (!prevFID.isValid()) {
      std::cout << file;
    } else {
      std::cout << prevFile << " -> " << file;
    }
    std::cout << "\n";
  }

  void EndOfMainFile() override {
    std::cout << "End of main file\n";
  }

};


class PrintFilesConsumer : public ASTConsumer {

 public:
  using ASTConsumerOptions = ASTPluginLib::PluginASTOptionsBase;
  using PreprocessorHandler = PrintFilesPreprocessorHandler;
  using PreprocessorHandlerData = ASTPluginLib::EmptyPreprocessorHandlerData;

  PrintFilesConsumer(const CompilerInstance &CI,
                     std::shared_ptr<ASTConsumerOptions> options,
                     std::shared_ptr<PreprocessorHandlerData> sharedData,
                     std::unique_ptr<std::string> outputFile) {
    std::cout << "Input file: "
              << options->normalizeSourcePath(options->inputFile.getFile().data())
              << "\n";
    std::cout << "Input file kind: " << options->inputFile.getKind().getLanguage() << "\n";
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
