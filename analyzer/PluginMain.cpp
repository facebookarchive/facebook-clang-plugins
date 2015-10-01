/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>

#include "PluginMainRegistry.h"

static std::vector<register_checker_callback_t> &getCallbacks() {
  static std::vector<register_checker_callback_t> callbacks;
  return callbacks;
}

extern "C" void add_register_checker_callback(register_checker_callback_t f) {
  getCallbacks().push_back(f);
}

extern "C" void clang_registerCheckers(clang::ento::CheckerRegistry &registry) {
  auto callbacks = getCallbacks();
  for (std::vector<register_checker_callback_t>::iterator it =
           callbacks.begin();
       it != callbacks.end();
       ++it) {
    (*it)(registry);
  }
}

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;
