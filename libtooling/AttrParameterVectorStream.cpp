/**
 * Copyright (c) 2015, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "AttrParameterVectorStream.h"

#include <llvm/Support/raw_ostream.h>

namespace ASTLib {

AttrParameterVectorStream &AttrParameterVectorStream::operator<<(
    const std::string &str) {
  // hack to get rid of spurious leading " "s
  if (str != " ") {
    Content.push_back(str);
  }
  return *this;
}

AttrParameterVectorStream &AttrParameterVectorStream::operator<<(
    const unsigned int x) {
  return operator<<(std::to_string(x));
}

AttrParameterVectorStream &AttrParameterVectorStream::operator<<(
    const clang::VersionTuple &verTup) {
  return operator<<(verTup.getAsString());
}

const std::vector<std::string> &AttrParameterVectorStream::getContent() {
  return Content;
}

} // end of namespace ASTLib
