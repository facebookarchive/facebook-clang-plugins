# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

#!/bin/bash -e
ydump -s <($(dirname "$0")/build/clang_ast_yojson_validator "$@")
