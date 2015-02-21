/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string>
#include <memory>

#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>

#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

using namespace clang;

class PluginASTActionBase {
protected:
  StringRef OutputPath;
  StringRef DeduplicationServicePath;
  StringRef RealOutputPath;
  StringRef BasePath;

  void SetBasePath(llvm::StringRef InputFile) {
    SmallString<1024> CurrentDir;
    if (llvm::sys::fs::current_path(CurrentDir)) {
      llvm::errs() << "Failed to retrieve current working directory\n";
      exit(1);
    }

    // Force absolute paths everywhere if InputFile was given absolute.
    if (InputFile.startswith("/")) {
      BasePath = CurrentDir.str();
    } else {
      BasePath = StringRef();
    }
  }

  void SetRealOutputPath(llvm::StringRef InputFile) {
    if (OutputPath.startswith("%")) {
      RealOutputPath = InputFile.str() + OutputPath.slice(1, OutputPath.size()).str();
    } else {
      RealOutputPath = OutputPath;
    }
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string>& args) {
    if (args.size() > 0) {
      OutputPath = args[0];
      if (args.size() > 1) {
        DeduplicationServicePath = args[1];
      }
    }
    return true;
  }

};


template <
  class T,
  bool Binary=0,
  bool RemoveFileOnSignal=1,
  bool UseTemporary=1,
  bool CreateMissingDirectories=0
>
class SimplePluginASTAction : public PluginASTAction, PluginASTActionBase {

 protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InputFile) {
    PluginASTActionBase::SetBasePath(InputFile);
    PluginASTActionBase::SetRealOutputPath(InputFile);

    llvm::raw_fd_ostream *OS =
      CI.createOutputFile(PluginASTActionBase::RealOutputPath,
                          Binary,
                          RemoveFileOnSignal,
                          "",
                          "",
                          UseTemporary,
                          CreateMissingDirectories);

    if (!OS) {
      return nullptr;
    }

    // /!\ T must make a local copy of the strings passed by reference here.
    return std::unique_ptr<ASTConsumer>(
      new T(CI,
            InputFile,
            PluginASTActionBase::BasePath,
            PluginASTActionBase::DeduplicationServicePath,
            *OS));
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string>& args) {
    return PluginASTActionBase::ParseArgs(CI, args);
  }

};

template <class T>
class NoOpenSimplePluginASTAction : public PluginASTAction, PluginASTActionBase {

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InputFile) {
    PluginASTActionBase::SetBasePath(InputFile);
    PluginASTActionBase::SetRealOutputPath(InputFile);

    // /!\ T must make a local copy of the strings passed by reference here.
    return std::unique_ptr<ASTConsumer>(
       new T(CI,
             InputFile,
             PluginASTActionBase::BasePath,
             PluginASTActionBase::DeduplicationServicePath,
             PluginASTActionBase::RealOutputPath));
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string>& args) {
    return PluginASTActionBase::ParseArgs(CI, args);
  }

};
