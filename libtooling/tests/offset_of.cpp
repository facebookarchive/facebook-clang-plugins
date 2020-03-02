/*
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stddef.h>

struct Parts {
    char code;
    char part[4];
};

size_t allOffsets(struct Parts p) {
    size_t sum = 0;
    sum += offsetof(struct Parts, code);
    for (int i = 0; i < 4; i++) {
        sum += offsetof(struct Parts, part[i]);
    }
    return sum;
}
