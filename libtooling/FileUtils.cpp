/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include <clang/AST/Decl.h>
#include <llvm/Support/Path.h>

#include "FileUtils.h"

namespace FileUtils {

  bool DeduplicationService::verifyKey(const std::string &key) {
    auto I = cache.find(key);
    auto E = cache.end();
    if (I != E) {
      return I->second;
    }

    std::hash<std::string> strhash;
    size_t hash = strhash(key);

    int len = servicePath.length() + 32;
    char *file = (char *)malloc(len);
    if (!file) {
      fprintf(stderr, "Memory allocation failed.\n");
      exit(-1);
    }
    snprintf(file, len, "%s/lock-%.16zx", servicePath.c_str(), hash);

    int fd = open(file, O_CREAT | O_EXCL, 0644);
    bool result = (fd > 0);
    if (result) {
      close(fd);
#ifdef DEBUG
      // Re-open to write the key being tagged.
      int fd = open(file, O_WRONLY, 0644);
      dprintf(fd, "%s\n", key.c_str());
      close(fd);
#endif
    }
    free(file);
    cache[key] = result;
    return result;
  }

  bool DeduplicationService::verifyDeclFileLocation(const clang::Decl &Decl) {
    // For now we only work at the top level, below a TranslationUnitDecl.
    const clang::DeclContext *DC = Decl.getDeclContext();
    if (!DC || !clang::isa<clang::TranslationUnitDecl>(*DC)) {
      return true;
    }
    // Avoid in particular LinkageSpec which can have misleading locations.
    if (!clang::isa<clang::NamedDecl>(Decl)) {
      return true;
    }
    clang::SourceLocation SpellingLoc = sourceManager.getSpellingLoc(Decl.getLocation());
    clang::PresumedLoc PLoc = sourceManager.getPresumedLoc(SpellingLoc);
    if (PLoc.isInvalid()) {
      return true;
    }
    if (llvm::StringRef(PLoc.getFilename()).endswith(".h")) {
      return verifyKey(normalizePath(basePath, PLoc.getFilename()));
    } else {
      return true;
    }
  }

  bool DeduplicationService::verifyStmtFileLocation(const clang::Stmt &Stmt) {
    clang::SourceLocation SpellingLoc = sourceManager.getSpellingLoc(Stmt.getLocStart());
    clang::PresumedLoc PLoc = sourceManager.getPresumedLoc(SpellingLoc);
    if (PLoc.isInvalid()) {
      return true;
    }
    if (llvm::StringRef(PLoc.getFilename()).endswith(".h")) {
      return verifyKey(normalizePath(basePath, PLoc.getFilename()));
    } else {
      return true;
    }
  }

  std::string normalizePath(const std::string &currentWorkingDirectory, const std::string &pathToNormalize) {
    llvm::SmallVector<char, 16> result;
    std::vector<std::string> elements;
    std::string path(pathToNormalize);
    int skip = 0;

    if (llvm::sys::path::is_relative(pathToNormalize)) {
      // Prepend currentWorkingDirectory to path (unless currentWorkingDirectory is empty).
      llvm::SmallVector<char, 16> vec(currentWorkingDirectory.begin(), currentWorkingDirectory.end());
      llvm::sys::path::append(vec, path);
      path = std::string(vec.begin(), vec.end());
    } else {
      // Else copy the separator to maintain an absolute path.
      result.append(1, pathToNormalize.front());
    }

    elements.push_back(llvm::sys::path::filename(path));

    while (llvm::sys::path::has_parent_path(path)) {
      path = llvm::sys::path::parent_path(path);
      const std::string &element(llvm::sys::path::filename(path));
      if (element == ".") {
        continue;
      }
      if (element == "..") {
        skip++;
        continue;
      }
      if (skip > 0) {
        skip--;
        continue;
      }
      elements.push_back(element);
    }
    while (skip > 0) {
      elements.push_back("..");
      skip--;
    }

    for (auto I = elements.rbegin(), E = elements.rend(); I != E; I++) {
      llvm::sys::path::append(result, *I);
    }
    return std::string(result.begin(), result.end());
  }

}