# Copyright (c) 2014, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

.PHONY: all clean test depend

LEVEL=..
include $(LEVEL)/Makefile.common

LIBTOOLING=$(LEVEL)/libtooling
ATDLIB=$(LEVEL)/libtooling/atdlib

# see README for the required versions
ATDGEN=atdgen
ATDCAT=atdcat -i

OCAMLOPT=ocamlfind ocamlopt -package unix,atdgen,camlzip -I build

all: build/clang_ast_yojson_validator build/clang_ast_converter build/clang_ast_named_decl_printer

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

# standard ocaml rules
build/%.cmx:%.ml
	$(OCAMLOPT) -o $@ -c $<

build/%.cmi:%.mli
	$(OCAMLOPT) -o $@ -c $<

build/%.cmx:build/%.ml
	$(OCAMLOPT) -o $@ -c $<

build/%.cmi:build/%.mli
	$(OCAMLOPT) -o $@ -c $<

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
PRINTER_TEST_FILES=ObjCTest.m
CONVERTER_TEST_FILE=Hello.m

# simple library for composing unix processes
build/process_test: build/process.cmx build/process_test.cmx
	$(OCAMLOPT) -linkpkg -o $@ $^

# Utilities
build/utils_test: build/process.cmx build/utils.cmx build/utils_test.cmx
	$(OCAMLOPT) -linkpkg -o $@ $^

CLANG_AST_LIBS=$(patsubst %,build/%.cmx,clang_ast_t clang_ast_j process utils yojson_utils)

build/yojson_utils_test: $(CLANG_AST_LIBS) build/yojson_utils_test.cmx
	$(OCAMLOPT) -linkpkg -o $@ $^

# AST parser and dumper
build/clang_ast_yojson_validator: $(CLANG_AST_LIBS) build/clang_ast_yojson_validator.cmx
	$(OCAMLOPT) -linkpkg -o $@ $^

# AST format converter
build/clang_ast_converter: $(CLANG_AST_LIBS) build/clang_ast_converter.cmx
	$(OCAMLOPT) -linkpkg -o $@ $^

# example of AST visitor
build/clang_ast_named_decl_printer: $(patsubst %,build/%.cmx,clang_ast_t clang_ast_j clang_ast_proj clang_ast_named_decl_printer)
	$(OCAMLOPT) -linkpkg -o $@ $^

test: $(patsubst %,build/%,process_test utils_test yojson_utils_test clang_ast_yojson_validator clang_ast_converter clang_ast_named_decl_printer)
	@make -C $(LIBTOOLING) $(PRINTER_TEST_FILES:%=build/ast_samples/%.yjson) $(TEST_FILES:%=build/ast_samples/%.yjson.gz) $(CONVERTER_TEST_FILE:%=build/ast_samples/%.yjson)
	@export LIMIT=100; \
	 $(RUNTEST) tests/process_test build/process_test; \
	 $(RUNTEST) tests/utils_test build/utils_test; \
	 $(RUNTEST) tests/yojson_utils_test build/yojson_utils_test; \
	 $(RUNTEST) tests/clang_ast_named_decl_printer build/clang_ast_named_decl_printer $(PRINTER_TEST_FILES:%=$(LIBTOOLING)/build/ast_samples/%.yjson); \
	 $(RUNTEST) tests/clang_ast_converter build/clang_ast_converter --pretty $(CONVERTER_TEST_FILE:%=$(LIBTOOLING)/build/ast_samples/%.yjson); \
	 $(RUNTEST) tests/clang_ast_yojson_validator build/clang_ast_yojson_validator $(TEST_FILES:%=$(LIBTOOLING)/build/ast_samples/%.yjson.gz)
	@if [ ! $$KEEP_TEST_OUTPUTS ]; then rm -f tests/*.out; fi

record-test-outputs:
	@rm -f tests/*.out
	@make DEBUG=1 KEEP_TEST_OUTPUTS=1 test || true
	@for F in tests/*.out; do cp $$F $${F%.out}.exp; done
	@rm -f tests/*.out

clean:
	@rm -f tests/*.out build/*

depend: $(wildcard *.ml) $(wildcard *.mli) build/clang_ast_t.mli build/clang_ast_t.ml build/clang_ast_j.mli build/clang_ast_j.ml
	ocamldep -I build $^ | sed -e 's/\([a-zA-Z0-9_]*\.cm.\)/build\/\1/g' | sed -e 's/build\/build\//build\//g' > .depend

include .depend
