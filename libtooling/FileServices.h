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

#include <map>
#include <string>

namespace FileServices {

  /**
   * Translation of source paths. Optionally use a temporary directory as
   * a key value store to retrieve the original path of copied headers.
   * Values have to be written in a separate reporter.
   */
  class TranslationService {
    const std::string servicePath;
    std::map<std::string, std::string> cache;

  public:
    TranslationService(const std::string &servicePath) : servicePath(servicePath) {}

    /**
     * Text files in servicePath will be read to retrieve the original source path in case of a copied file.
     */
    void recordCopiedFile(const std::string &copiedPath, const std::string &realPath);

    /**
     * Text files in servicePath will be read to retrieve the original source path in case of a copied file.
     */
    const std::string &findOriginalFile(const std::string &pathToNormalize);
  };

}
