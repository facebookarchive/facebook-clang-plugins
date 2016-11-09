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

#include <functional>
#include <memory>
#include <stdlib.h>
#include <string>
#include <unordered_map>

#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>

#include "FileServices.h"
#include "FileUtils.h"

namespace ASTPluginLib {

struct PluginASTOptionsBase {
  // source file being parsed
  clang::FrontendInputFile inputFile;
  // output file for the plugin
  std::string outputFile;
  // object file produced by the usual frontend (possibly empty)
  std::string objectFile;

  /* Will contain the current directory if PREPEND_CURRENT_DIR was specified.
   * The intention is to make file paths in the AST absolute if needed.
   */
  std::string basePath;

  /* Configure a second pass on file paths to make them relative to the repo
   * root. */
  std::string repoRoot;
  /* Configure a third pass on (absolute) file paths to blank the system root:
   *    /path/to/sysroot/usr/lib/foo.h --> /usr/lib/foo.h
   */
  std::string iSysRoot;
  /* Configure a fourth pass on (absolute) file paths to detect siblings to
   * the repo root. If the repo root is /some/path, /some/other_path will be
   * rewritten ../other_path
   */
  bool allowSiblingsToRepoRoot = false;
  /* Whether file paths that could not be normalized by any of the rules above
   * should be kept or blanked.
   */
  bool keepExternalPaths = false;
  /* Resolve symlinks to their real path. */
  bool resolveSymlinks = false;

  /* Deduplication service: whether certain files should be visited once. */
  std::unique_ptr<FileServices::DeduplicationService> deduplicationService;

  /* Translation service: whether certain copied source files should be
   * translated back to the original name. */
  std::unique_ptr<FileServices::TranslationService> translationService;

  typedef std::unordered_map<std::string, std::string> argmap_t;

  static argmap_t makeMap(const std::vector<std::string> &args);

 private:
  /* cache for normalizeSourcePath */
  std::unique_ptr<std::unordered_map<const char *, std::string>>
      normalizationCache;

 protected:
  static const std::string envPrefix;

  static bool loadString(const argmap_t &map,
                         const char *key,
                         std::string &val);

  static bool loadBool(const argmap_t &map, const char *key, bool &val);

  static bool loadInt(const argmap_t &map, const char *key, long &val);

  static bool loadUnsignedInt(const argmap_t &map,
                              const char *key,
                              unsigned long &val);

 public:
  PluginASTOptionsBase() {
    normalizationCache.reset(
        new std::unordered_map<const char *, std::string>());
  };

  void loadValuesFromEnvAndMap(const argmap_t map);

  // This should be called after outputFile has been set, so as to finalize
  // the output file in case a pattern "%.bla" was given.
  void setObjectFile(const std::string &path);

  const std::string &normalizeSourcePath(const char *path) const;
};

template <class PluginASTOptions = PluginASTOptionsBase>
class SimplePluginASTActionBase : public clang::PluginASTAction {
 protected:
  std::unique_ptr<PluginASTOptions> Options;

  // Called when FrontendPluginRegistry is used.
  virtual bool ParseArgs(const clang::CompilerInstance &CI,
                         const std::vector<std::string> &args_) {
    std::vector<std::string> args = args_;
    Options = std::unique_ptr<PluginASTOptions>(new PluginASTOptions());
    if (args.size() > 0) {
      Options->outputFile = args[0];
      args.erase(args.begin());
    }
    Options->loadValuesFromEnvAndMap(PluginASTOptions::makeMap(args));
    return true;
  }

  SimplePluginASTActionBase() {}

  // Alternate constructor to pass an optional sequence "KEY=VALUE,.."
  // expected to be use with SimpleFrontendActionFactory below.
  explicit SimplePluginASTActionBase(const std::vector<std::string> &args) {
    Options = std::unique_ptr<PluginASTOptions>(new PluginASTOptions());
    Options->loadValuesFromEnvAndMap(PluginASTOptions::makeMap(args));
  }

  bool SetFileOptions(clang::CompilerInstance &CI, llvm::StringRef inputFilename) {
    // When running clang tool on more than one source file, CreateASTConsumer
    // will be ran for each of them separately. Hence, Inputs.size() = 1.
    clang::FrontendInputFile inputFile = CI.getFrontendOpts().Inputs[0];

    switch (inputFile.getKind()) {
    case clang::IK_None:
    case clang::IK_Asm:
    case clang::IK_LLVM_IR:
      // We can't do anything with these - they may trigger errors when running
      // clang frontend
      return false;
    default:
      // run the consumer for IK_AST and all others
      break;
    }
    if (Options == nullptr) {
      Options = std::unique_ptr<PluginASTOptions>(new PluginASTOptions());
      Options->loadValuesFromEnvAndMap(
          std::unordered_map<std::string, std::string>());
    }
    Options->inputFile = inputFile;
    Options->setObjectFile(CI.getFrontendOpts().OutputFile);
    // success
    return true;
  }
};

template <class SimpleASTAction>
class SimpleFrontendActionFactory
    : public clang::tooling::FrontendActionFactory {
  std::vector<std::string> args_;

 public:
  explicit SimpleFrontendActionFactory(std::vector<std::string> args)
      : args_(args) {}

  clang::FrontendAction *create() override {
    return new SimpleASTAction(args_);
  }
};

template <class T,
          class PluginASTOptions = PluginASTOptionsBase,
          bool Binary = 0,
          bool RemoveFileOnSignal = 1,
          bool UseTemporary = 1,
          bool CreateMissingDirectories = 0>
class SimplePluginASTAction
    : public SimplePluginASTActionBase<PluginASTOptions> {
  typedef SimplePluginASTActionBase<PluginASTOptions> Parent;

 public:
  SimplePluginASTAction() {}

  explicit SimplePluginASTAction(const std::vector<std::string> &args)
      : SimplePluginASTActionBase<PluginASTOptions>(args) {}

 protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance &CI, llvm::StringRef inputFilename) {
    if (!Parent::SetFileOptions(CI, inputFilename)) {
      return nullptr;
    }
    std::unique_ptr<llvm::raw_ostream> OS =
        CI.createOutputFile(Parent::Options->outputFile,
                            Binary,
                            RemoveFileOnSignal,
                            "",
                            "",
                            UseTemporary,
                            CreateMissingDirectories);
    if (!OS) {
      return nullptr;
    }
    return std::unique_ptr<clang::ASTConsumer>(
        new T(CI, std::move(Parent::Options), std::move(OS)));
  }
};

template <class T, class PluginASTOptions = PluginASTOptionsBase>
class NoOpenSimplePluginASTAction
    : public SimplePluginASTActionBase<PluginASTOptions> {
  typedef SimplePluginASTActionBase<PluginASTOptions> Parent;

 public:
  NoOpenSimplePluginASTAction() {}

  explicit NoOpenSimplePluginASTAction(const std::vector<std::string> &args)
      : SimplePluginASTActionBase<PluginASTOptions>(args) {}

 protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance &CI, llvm::StringRef inputFilename) {
    if (!Parent::SetFileOptions(CI, inputFilename)) {
      return nullptr;
    }
    std::unique_ptr<std::string> outputFile = std::unique_ptr<std::string>(
        new std::string(Parent::Options->outputFile));
    return std::unique_ptr<clang::ASTConsumer>(
        new T(CI, std::move(Parent::Options), std::move(outputFile)));
  }
};
}
