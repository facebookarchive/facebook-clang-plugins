# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

.PHONY: clean all test

LEVEL=../..
include $(LEVEL)/Makefile.common

all: build/jsontest

build/jsontest: tests/jsontest.cpp ATDWriter.h
	@mkdir -p build
	$(CXX) $(CFLAGS) $< -o $@

test: build/jsontest extract_atd_from_cpp.py normalize_names_in_atd.py
	@$(RUNTEST) tests/jsontest build/jsontest
	@$(RUNTEST) tests/extract_test.cpp python extract_atd_from_cpp.py tests/extract_test.cpp
	@$(RUNTEST) tests/normalize_test.atd python normalize_names_in_atd.py tests/normalize_test.atd
	@if [ ! $$KEEP_TEST_OUTPUTS ]; then rm -f tests/*.out; fi

record-test-outputs:
	@rm -f tests/*.out
	@make DEBUG=1 KEEP_TEST_OUTPUTS=1 test || true
	@for F in tests/*.out; do cp $$F $${F%.out}.exp; done
	@rm -f tests/*.out

clean:
	@rm -rf build/* tests/*.o tests/*.out
