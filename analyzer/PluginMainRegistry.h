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

#include <clang/StaticAnalyzer/Core/CheckerRegistry.h>

typedef void (*register_checker_callback_t)(
    clang::ento::CheckerRegistry &registry);

extern "C" void add_register_checker_callback(register_checker_callback_t f);

#define REGISTER_CHECKER_IN_PLUGIN(class_name, name_str, description_str) \
  void register_##class_name(::clang::ento::CheckerRegistry &registry) {  \
    registry.addChecker<class_name>(name_str, description_str);           \
  }                                                                       \
                                                                          \
  __attribute__((constructor)) static void init_registration() {          \
    add_register_checker_callback(&register_##class_name);                \
  }
