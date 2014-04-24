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
#include <clang/Frontend/CompilerInstance.h>

#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

using namespace clang;

template <
  class T,
  bool Binary=0,
  bool RemoveFileOnSignal=1,
  bool UseTemporary=1,
  bool CreateMissingDirectories=0,
  bool TakePercentAsInputFile=1
>
class SimplePluginASTAction : public PluginASTAction {
  StringRef OutputPath;

 protected:
  ASTConsumer *CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InputFile) {
    llvm::raw_fd_ostream *OS = NULL;

    if (TakePercentAsInputFile && OutputPath.startswith("%")) {
      // Take the remaining of OutputPath as a suffix to append to InputFile.
      // (Purposely keep the existing suffix of InputFile.)
      OS = CI.createOutputFile(InputFile.str() + OutputPath.slice(1, OutputPath.size()).str(),
                               Binary,
                               RemoveFileOnSignal,
                               "",
                               "",
                               UseTemporary,
                               CreateMissingDirectories);
    } else {
      // Use stdout if OutputPath == "" or "-", the given file name otherwise.
      OS = CI.createOutputFile(OutputPath,
                               Binary,
                               RemoveFileOnSignal,
                               "",
                               "",
                               UseTemporary,
                               CreateMissingDirectories);
    }
    if (!OS) {
      return NULL;
    }
    return new T(CI, InputFile, *OS);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string>& args) {
    if (args.size() > 0) {
      OutputPath = args[0];
    }
    return true;
  }

};
