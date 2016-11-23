# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

.PHONY: all test clean xcode

LEVEL=.
include Makefile.common

ifneq "$(CLANG_PLUGINS_EXTRA_REPO)" ""
-include $(CLANG_PLUGINS_EXTRA_REPO)/Makefile.include
CMAKE_SETTINGS+=-D CLANG_PLUGINS_EXTRA_REPO="$(CLANG_PLUGINS_EXTRA_REPO)"
endif

CMAKE_SETTINGS+=-D CMAKE_C_COMPILER="$(CC)"
CMAKE_SETTINGS+=-D CMAKE_CXX_COMPILER="$(CXX)"
CMAKE_SETTINGS+=-D CMAKE_CXX_FLAGS="$(CFLAGS)"
CMAKE_SETTINGS+=-D CMAKE_SHARED_LINKER_FLAGS="$(LDFLAGS_DYLIB)"
CMAKE_SETTINGS+=-D CMAKE_EXE_LINKER_FLAGS="$(LLVM_LDFLAGS) $(CLANG_TOOL_LIBS) $(LDFLAGS) -lz -lpthread -lm"

all:
	$(MAKE) -C analyzer all
	$(MAKE) -C libtooling/atdlib all
	$(MAKE) -C libtooling all

test: all
	$(MAKE) -C analyzer test
	$(MAKE) -C libtooling/atdlib test
	$(MAKE) -C libtooling test

clean:
	$(MAKE) -C analyzer clean
	$(MAKE) -C libtooling/atdlib clean
	$(MAKE) -C libtooling clean
	rm -rf xcode *~

xcode: CMakeLists.txt libtooling/CMakeLists.txt analyzer/CMakeLists.txt
	mkdir -p xcode && cd xcode && cmake -G Xcode $(CMAKE_SETTINGS) ..

# Local Variables:
# mode: makefile
# End:
