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
#include <fstream>
#include <istream>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "FileServices.h"

namespace FileServices {

  std::string create_filename(const std::string &prefix, const std::string &servicePath, const std::string &key) {
    int len = servicePath.length() + prefix.length() + 19;
    char file[len];

    // NOTE: This requires sizeof(size_t) == 8 and a correct implementation of std::hash.
    std::hash<std::string> strhash;
    size_t hash = strhash(key);

    snprintf(file, len, "%s/%s-%.16zx", servicePath.c_str(), prefix.c_str(), hash);
    return file;
  }

  const std::string &TranslationService::findOriginalFile(const std::string &path) {
    auto I = cache.find(path);
    if (I != cache.end()) {
      return I->second;
    }

    std::string &result = cache[path];
    std::string file = create_filename("copy", servicePath, path);
    std::ifstream fin(file);
    if (fin.is_open()) {
      // Read the real path behind a copied path.
      std::getline(fin, result);
    } else {
      result = path;
    }

    return result;
  }

  void TranslationService::recordCopiedFile(const std::string &copiedPath, const std::string &realPath) {
    std::string file = create_filename("copy", servicePath, copiedPath);
    int fd = open(file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    dprintf(fd, "%s\n", realPath.c_str());
    close(fd);
  }

}
