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
#include <clang/AST/Decl.h>

#include "FileServices.h"

namespace FileUtils {

  /**
   * Simplify away "." and ".." elements.
   * If pathToNormalize is a relative path, it will be pre-pended with currentWorkingDirectory unless currentWorkingDirectory == "".
   */
  std::string makeAbsolutePath(const std::string &currentWorkingDirectory, std::string pathToNormalize);

  /**
   * Try to delete a prefix "repoRoot/" from the given absolute path. Return the same path otherwise.
   */
  std::string makeRelativePath(const std::string &repoRoot, const std::string &path, bool keepExternalPaths);

}
