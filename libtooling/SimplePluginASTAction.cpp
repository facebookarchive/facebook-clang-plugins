/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include <stdlib.h>

#include <llvm/Support/Path.h>

#include "FileServices.h"
#include "FileUtils.h"
#include "SimplePluginASTAction.h"

namespace ASTPluginLib {

  /**
   * The actual prefix to prepend to environment variables (but not for the command line).
   */
  const std::string PluginASTOptionsBase::envPrefix = "CLANG_FRONTEND_PLUGIN__";

  PluginASTOptionsBase::argmap_t PluginASTOptionsBase::makeMap(const std::vector<std::string> &args) {
    argmap_t map;
    for (auto arg : args) {
      size_t pos = arg.find('=');
      if (pos != std::string::npos) {
        std::string key = arg.substr(0, pos);
        std::string value = arg.substr(pos + 1);
        map[key] = value;
      }
    }
    return map;
  }

  bool PluginASTOptionsBase::loadString(const argmap_t &map, const char *key, std::string &val) {
    auto I = map.find(key);
    if (I != map.end()) {
      val = I->second;
      return true;
    }
    const char *s = getenv((envPrefix + key).c_str());
    if (s != nullptr) {
      val = s;
      return true;
    }
    return false;
  }

  bool PluginASTOptionsBase::loadBool(const argmap_t &map, const char *key, bool &val) {
    std::string s_val;
    if (loadString(map, key, s_val)) {
      val = (bool)strtol(s_val.c_str(), nullptr, 10);
      return true;
    }
    return false;
  }

  void PluginASTOptionsBase::loadValuesFromEnvAndMap(const argmap_t map) {
    bool needBasePath = false;
    std::string tempDirDeduplication;
    std::string tempDirTranslation;

    // Possibly override the first argument given on the command line.
    loadString(map, "OUTPUT_FILE", outputFile);

    loadBool(map, "PREPEND_CURRENT_DIR", needBasePath);
    loadString(map, "MAKE_RELATIVE_TO", repoRoot);
    loadBool(map, "KEEP_EXTERNAL_PATHS", keepExternalPaths);

    loadString(map, "USE_TEMP_DIR_FOR_DEDUPLICATION", tempDirDeduplication);
    loadString(map, "USE_TEMP_DIR_FOR_COPIED_PATHS", tempDirTranslation);

    if (needBasePath) {
      llvm::SmallString<1024> CurrentDir;
      if (llvm::sys::fs::current_path(CurrentDir)) {
        llvm::errs() << "Failed to retrieve current working directory\n";
      } else {
        basePath = CurrentDir.str();
      }
    }

    if (tempDirDeduplication != "") {
      deduplicationService.reset(new FileServices::DeduplicationService(tempDirDeduplication));
    }
    if (tempDirTranslation != "") {
      translationService.reset(new FileServices::TranslationService(tempDirTranslation));
    }
  }

  void PluginASTOptionsBase::setObjectFile(const std::string &path) {
    objectFile = path;
    if (path != "" && outputFile.size() > 0 && outputFile[0] == '%') {
      outputFile = path + outputFile.substr(1);
    }
  }

  std::string PluginASTOptionsBase::normalizeSourcePath(std::string path) const {
    if (basePath == "") {
      return path;
    }
    std::string absPath = FileUtils::makeAbsolutePath(basePath, path);
    const std::string &realPath =
      translationService != nullptr ? translationService->findOriginalFile(absPath) : absPath;
    if (repoRoot == "") {
      return realPath;
    }
    return FileUtils::makeRelativePath(repoRoot, realPath, keepExternalPaths);
  }

}
