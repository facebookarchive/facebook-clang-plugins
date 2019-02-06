# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

.PHONY: all test clean

LEVEL=.
include Makefile.common

all:
	$(MAKE) -C libtooling/atdlib all
	$(MAKE) -C libtooling all

test: all
	$(MAKE) -C libtooling/atdlib test
	$(MAKE) -C libtooling test

clean:
	$(MAKE) -C libtooling/atdlib clean
	$(MAKE) -C libtooling clean
