/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <clang/AST/AST.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Path.h>
#include <vector>

#include "FileUtils.h"

namespace FileUtils {

/**
 * Simplify away "." and ".." elements.
 * If pathToNormalize is a relative path, it will be pre-pended with
 * currentWorkingDirectory unless currentWorkingDirectory == "".
 */
std::string makeAbsolutePath(const std::string &currentWorkingDirectory,
                             std::string path) {
  llvm::SmallVector<char, 16> result;
  std::vector<std::string> elements;
  int skip = 0;

  if (llvm::sys::path::is_relative(path)) {
    // Prepend currentWorkingDirectory to path (unless currentWorkingDirectory
    // is empty).
    llvm::SmallVector<char, 16> vec(currentWorkingDirectory.begin(),
                                    currentWorkingDirectory.end());
    llvm::sys::path::append(vec, path);
    path = std::string(vec.begin(), vec.end());
  } else {
    // Else copy the separator to maintain an absolute path.
    result.append(1, path.front());
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

std::string makeRelativePath(const std::string &repoRoot,
                             const std::string &sysRoot,
                             bool keepExternalPaths,
                             bool allowSiblingsToRepoRoot,
                             const std::string &path) {
  if (repoRoot != "") {
    if (llvm::StringRef(path).startswith(repoRoot + "/")) {
      return path.substr(repoRoot.size() + 1);
    }
    if (allowSiblingsToRepoRoot) {
      std::string parentOfRoot = llvm::sys::path::parent_path(repoRoot);
      if (llvm::StringRef(path).startswith(parentOfRoot + "/")) {
        return "../" + path.substr(parentOfRoot.size() + 1);
      }
    }
  }
  if (sysRoot != "" && llvm::StringRef(path).startswith(sysRoot + "/")) {
    // Intentionally keep the heading "/" in this case.
    return path.substr(sysRoot.size());
  }
  if (keepExternalPaths) {
    return path;
  }
  return "";
}

bool shouldTraverseDeclFile(FileServices::DeduplicationService &DedupService,
                            const std::string &BasePath,
                            const clang::SourceManager &SM,
                            const clang::Decl &Decl) {
  // For now we only work at the top level, below a TranslationUnitDecl.
  const clang::DeclContext *DC = Decl.getDeclContext();
  if (!DC || !clang::isa<clang::TranslationUnitDecl>(*DC)) {
    return true;
  }
  // Skip only NamedDecl's. Avoid in particular LinkageSpec's which can have
  // misleading locations.
  if (!clang::isa<clang::NamedDecl>(Decl)) {
    return true;
  }
  // Do not skip Namespace, ClassTemplate/Specialization, Using, UsingShadow,
  // CXXRecord declarations.
  // (CXXRecord mostly because they can contain UsingShadow declarations.)
  if (clang::isa<clang::NamespaceDecl>(Decl) ||
      clang::isa<clang::ClassTemplateDecl>(Decl) ||
      clang::isa<clang::ClassTemplateSpecializationDecl>(Decl) ||
      clang::isa<clang::FunctionTemplateDecl>(Decl) ||
      clang::isa<clang::UsingDecl>(Decl) ||
      clang::isa<clang::UsingShadowDecl>(Decl) ||
      clang::isa<clang::CXXRecordDecl>(Decl)) {
    return true;
  }
  clang::SourceLocation SpellingLoc = SM.getSpellingLoc(Decl.getLocation());
  clang::PresumedLoc PLoc = SM.getPresumedLoc(SpellingLoc);
  if (PLoc.isInvalid()) {
    return true;
  }
  if (llvm::StringRef(PLoc.getFilename()).endswith(".h")) {
    return DedupService.verifyKey(
        FileUtils::makeAbsolutePath(BasePath, PLoc.getFilename()));
  } else {
    return true;
  }
}
}
