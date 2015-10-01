/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * Command line tool to populate the database of
 * FileServices::TranslationService
 */

#include "FileServices.h"
#include "stdio.h"

int main(int argc, const char **argv) {
  if (argc != 4) {
    printf("Usage: record_copied_file TMP_DIR COPIED_PATH REAL_PATH\n");
  }

  FileServices::TranslationService service(argv[1]);
  service.recordCopiedFile(argv[2], argv[3]);
  return 0;
}
