# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

.PHONY: clean all test test-quiet record-test-outputs

LEVEL=..
include $(LEVEL)/Makefile.common

# plugin framework and project libraries
HEADERS+=PluginMainRegistry.h
OBJS=PluginMain.o

# ObjCARCQualifierChecker
CHECKERS+=facebook.ObjCARCQualifierChecker
OBJS+=ObjCARCQualifierChecker.o

# DanglingDelegateChecker
HEADERS+=DanglingDelegateCommon.h DanglingDelegateFactFinder.h
CHECKERS+=facebook.DanglingDelegateChecker
OBJS+=DanglingDelegateCommon.o DanglingDelegateFactFinder.o DanglingDelegateChecker.o

# GlobalOrStaticVariableForDispatchOnceChecker
CHECKERS+=facebook.GlobalOrStaticVariableForDispatchOnceChecker
OBJS+=GlobalOrStaticVariableForDispatchOnceChecker.o

# PropertySynthesizeChecker
CHECKERS+=facebook.PropertySynthesizeChecker
OBJS+=PropertySynthesizeChecker.o

# SuspiciousRespondsToSelectorChecker
CHECKERS+=facebook.SuspiciousRespondsToSelectorChecker
OBJS+=SuspiciousRespondsToSelectorChecker.o


all: build/FacebookAnalyzerPlugin.dylib

# hook to optional external sources
include $(LEVEL)/Makefile.extra
ifneq "$(EXTRA_DIR)" ""
include $(EXTRA_DIR)/Makefile.include
endif

include $(LEVEL)/Makefile.rules

build/FacebookAnalyzerPlugin.dylib: $(OBJS:%=build/%)
	@mkdir -p build
	$(CXX) $(LDFLAGS_DYLIB) -o $@  $^ -lz -lpthread -lm

CC1_PLUGIN_ARGS=-load $(shell pwd)/build/FacebookAnalyzerPlugin.dylib -analyze $(CHECKERS:%=-analyzer-checker=%)
PLUGIN_ARGS=$(CC1_PLUGIN_ARGS:%=-Xclang %)

TEST_DIRS=tests
ifneq "$(EXTRA_DIR)" ""
TEST_DIRS+=$(EXTRA_DIR)/tests
endif

OUT_TEST_FILES= ${TEST_DIRS:%=%/*.out} ${TEST_DIRS:%=%_arc/*.out} ${TEST_DIRS:%=%_noinline/*.out}
C_TEST_FILES=${TEST_DIRS:%=%/*.c}
CXX_TEST_FILES=${TEST_DIRS:%=%/*.cpp}
ifdef IOSFLAGS
IOS_TEST_FILES=${TEST_DIRS:%=%/*.m} ${TEST_DIRS:%=%/*.mm}
IOS_ARC_TEST_FILES=${TEST_DIRS:%=%_arc/*.m} ${TEST_DIRS:%=%_arc/*.mm}
IOS_NOINLINE_TEST_FILES=${TEST_DIRS:%=%_noinline/*.m} ${TEST_DIRS:%=%_noinline/*.mm}
else
IOS_TEST_FILES=
IOS_ARC_TEST_FILES=
IOS_NOINLINE_TEST_FILES=
endif

test: all
	@for F in $(C_TEST_FILES); do $(RUNTEST) $$F $(CLANG) --analyze $(PLUGIN_ARGS) $$F; done; \
	 for F in $(CXX_TEST_FILES); do $(RUNTEST) $$F $(CLANG) --analyze $(PLUGIN_ARGS) $$F; done; \
	 for F in $(IOS_TEST_FILES); do $(RUNTEST) $$F $(CLANG) --analyze $(IOSFLAGS) $(PLUGIN_ARGS) $$F; done; \
	 for F in $(IOS_ARC_TEST_FILES); do $(RUNTEST) $$F $(CLANG) --analyze $(IOSFLAGS) -fobjc-arc $(PLUGIN_ARGS) $$F; done; \
	 for F in $(IOS_NOINLINE_TEST_FILES); do $(RUNTEST) $$F $(CLANG) --analyze $(IOSFLAGS) -Xclang -analyzer-config -Xclang ipa=none $(PLUGIN_ARGS) $$F; done; \
	 rm -f *.plist
	@if [ ! $$KEEP_TEST_OUTPUTS ]; then rm -f $(OUT_TEST_FILES); fi

dump-test-ast:
	@for F in $(C_TEST_FILES); do $(CLANG) -cc1 -fsyntax-only -ast-dump $$F; done
	@for F in $(IOS_TEST_FILES); do $(CLANG) -Xclang -fsyntax-only -Xclang -ast-dump $(IOSFLAGS) $$F; done
	@for F in $(IOS_ARC_TEST_FILES); do $(CLANG) -Xclang -fsyntax-only -Xclang -ast-dump $(IOSFLAGS) -fobjc-arc $$F; done
	@for F in $(IOS_NOINLINE_TEST_FILES); do $(CLANG) -Xclang -fsyntax-only -Xclang -ast-dump $(IOSFLAGS) $$F; done

# Testing equality of outputs is a little fragile but this makes it easy to (re)generate the .exp files
# Cheaters won't have a chance in phabricator anyway ;-)
record-test-outputs:
	@$(MAKE) DEBUG=1 KEEP_TEST_OUTPUTS=1 test || true
	@for F in $(OUT_TEST_FILES); do cp $$F $${F%.out}.exp; done
	@rm -f $(OUT_TEST_FILES)

clean:
	@rm -f build/* *.plist $(OUT_TEST_FILES)
