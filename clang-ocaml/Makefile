# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

.PHONY: all clean test

LEVEL=..
include $(LEVEL)/Makefile.common

LIBTOOLING=$(LEVEL)/libtooling
ATDLIB=$(LEVEL)/libtooling/atdlib

# see README for the required versions
ATDGEN=atdgen -rec
ATDCAT=atdcat -i

all: build/clang_ast_yojson_validator build/clang_ast_named_decl_printer

# type definitions
%_t.ml %_t.mli: %.atd
	$(ATDGEN) -t -o $* $<

# yojson
%_j.ml %_j.mli: %.atd
	$(ATDGEN) -j -o $* $<

# biniou
%_b.ml %_b.mli: %.atd
	$(ATDGEN) -b -o $* $<

# validator
%_v.ml %_v.mli: %.atd
	$(ATDGEN) -v -o $* $<

# link sources to build when needed (ocamlfind wants sources next to products)
build/%:%
	@mkdir -p build
	cd build; ln -sf ../$* $*

# clang AST
build/ast_inline.atd.p: $(LIBTOOLING)/ASTExporter.cpp
	@mkdir -p build
	@make -C $(LIBTOOLING) build/ASTExporter.cpp.p
	python $(ATDLIB)/extract_atd_from_cpp.py $(LIBTOOLING)/build/ASTExporter.cpp.p | python $(ATDLIB)/normalize_names_in_atd.py > $@

build/ast_inline.atd: build/ast_inline.atd.p
	cpp -P -w $< > $@

build/clang_ast.atd: build/ast_inline.atd
	$(ATDCAT) $< > $@

build/ast_inline.atd.inc: build/ast_inline.atd.p
	cat $< | grep '^#' > $@

build/ast_inline.pat.inc: build/ast_inline.atd.inc
	cat $< | sed 's/*/,/g; s/ list/_list/g' > $@

build/clang_ast_proj.mli: clang_ast_proj.mli.p build/ast_inline.atd.inc
	cat $< | cpp -P -w -include build/ast_inline.atd.inc > $@

build/clang_ast_proj.ml: clang_ast_proj.ml.p build/ast_inline.pat.inc
	cpp -P -I$(CLANG_PREFIX)/include $< | python $(ATDLIB)/normalize_names_in_atd.py | cpp -P -w -include build/ast_inline.pat.inc > $@

TEST_FILES=ASTExporter.cpp Hello.m ObjCTest.m
PRINTER_TEST_FILES=ObjcTest.m

# AST parser and dumper
build/clang_ast_yojson_validator: build/clang_ast_t.mli build/clang_ast_t.ml build/clang_ast_j.mli build/clang_ast_j.ml build/clang_ast_yojson_validator.ml
	ocamlfind ocamlopt -I build -o $@ $^ -package atdgen -linkpkg

# For a change this .exp file is computed (using ydump to reformat the original json)
build/clang_ast_yojson_validator.exp: $(wildcard $(LIBTOOLING)/tests/*.m) $(LIBTOOLING)/ASTExporter.cpp
	@make DEBUG=1 -C $(LIBTOOLING) $(TEST_FILES:%=build/ast_samples/%.yjson)
	@rm -f $@
	@for F in $(TEST_FILES:%=$(LIBTOOLING)/build/ast_samples/%.yjson); do ydump -u $$F >> $@; done

# example of AST visitor
build/clang_ast_named_decl_printer: build/clang_ast_t.mli build/clang_ast_t.ml build/clang_ast_j.mli build/clang_ast_j.ml build/clang_ast_proj.mli build/clang_ast_proj.ml build/clang_ast_named_decl_printer.ml
	ocamlfind ocamlopt -I build -o $@ $^ -package atdgen -linkpkg

test: build/clang_ast_yojson_validator build/clang_ast_named_decl_printer build/clang_ast_yojson_validator.exp
	@make -C $(LIBTOOLING) $(TEST_FILES:%=build/ast_samples/%.yjson)
	@export LIMIT=100; \
	 $(RUNTEST) build/clang_ast_yojson_validator bash clang_ast_yojson_validator.sh $(TEST_FILES:%=$(LIBTOOLING)/build/ast_samples/%.yjson); \
	 $(RUNTEST) tests/clang_ast_named_decl_printer build/clang_ast_named_decl_printer $(PRINTER_TEST_FILES:%=$(LIBTOOLING)/build/ast_samples/%.yjson)
	@if [ ! $$KEEP_TEST_OUTPUTS ]; then rm -f tests/*.out; fi

record-test-outputs:
	@rm -f tests/*.out
	@make DEBUG=1 KEEP_TEST_OUTPUTS=1 test || true
	@for F in tests/*.out; do cp $$F $${F%.out}.exp; done
	@rm -f tests/*.out

clean:
	@rm -f tests/*.out build/*
