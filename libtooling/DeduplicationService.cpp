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

#include "DeduplicationService.h"

bool DeduplicationService::verifyKey(const std::string &key) {
  auto I = cache.find(key);
  auto E = cache.end();
  if (I != E) {
    return I->second;
  }

  std::hash<std::string> strhash;
  size_t hash = strhash(key);

  std::string file = servicePath + "/lock" + std::to_string(hash);

  int fd = open(file.c_str(), O_CREAT | O_EXCL, 0644);
  bool result = (fd > 0);
  if (result) {
#ifdef DEBUG
    close(fd);
    int fd = open(file.c_str(), O_WRONLY, 0644);
    dprintf(fd, "%s\n", key.c_str());
    close(fd);
#endif
  }
  cache[key] = result;
  return result;
}