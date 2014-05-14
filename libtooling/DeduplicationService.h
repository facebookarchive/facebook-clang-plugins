/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <string>
#include <map>

/**
 * Simple class to avoid duplicating outputs when a frontend plugin is run independently on multiple files.
 * Currently we simply use lock files in a tmp directory 'servicePath'.
 * This tmp directory must be setup and cleaned correctly outside this code.
 */
class DeduplicationService {
  const std::string servicePath;
  std::map<std::string, bool> cache;

public:
  DeduplicationService(const std::string &servicePath) : servicePath(servicePath) {}

  /* Returns true if we can proceed with the data corresponding to the key.
   * From then on, other clients (processes, etc) will get false for the same key.
   */
  bool verifyKey(const std::string &key);
};
