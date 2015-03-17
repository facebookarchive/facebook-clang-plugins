# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

set(LEVEL ..)

# import source files from the extra repo (if any)
if (DEFINED CLANG_PLUGINS_EXTRA_REPO)

  set(EXTRA_DIR "${LEVEL}/${CLANG_PLUGINS_EXTRA_REPO}/libtooling")
  # we expect the included file to set LIBTOOLING_PLUGIN_EXTRA_SOURCES
  include(${EXTRA_DIR}/CMakeLists.include)

  set(EXTRA_SOURCES)
  foreach(p ${LIBTOOLING_PLUGIN_EXTRA_SOURCES})
    set(EXTRA_SOURCES ${EXTRA_SOURCES} ${EXTRA_DIR}/${p})
  endforeach()

  include_directories(.)

else (DEFINED CLANG_PLUGINS_EXTRA_REPO)

  set(EXTRA_SOURCES)

endif (DEFINED CLANG_PLUGINS_EXTRA_REPO)

add_library(
  FacebookClangPlugin SHARED

  SimplePluginASTAction.h
  SimplePluginASTAction.cpp
  FileUtils.h
  FileUtils.cpp
  FileServices.h
  FileServices.cpp
  atdlib/ATDWriter.h
  ASTExporter.h
  ASTExporter.cpp
  ${EXTRA_SOURCES}
)

target_link_libraries(
  FacebookClangPlugin
)

add_executable(record_copied_file
  FileServices.h
  FileServices.cpp
  record_copied_file.cpp
)
