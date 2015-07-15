Ocaml frontend to the Clang AST
-------------------------------

Additionaly requirements:
- ocaml >= 4.01
- camlzip
- yojson
- atd >= 1.1.2
- atdgen >= 1.6.0

The simplest way to install these dependencies is
1) to install ocaml and opam using your system package manager (e.g. homebrew on MAC OS).
2) run 'opam install camlzip yojson atd atdgen'

Assuming that the current dir is the root of the git repository and CLANG_PREFIX=/usr/local, you may compile and run tests with
```
export CLANG_PREFIX=/usr/local
make -C clang-ocaml depend
make -C clang-ocaml test
```

How this works:
- The plugin YojsonASTExporter defined in libtooling/ASTExporter.cpp outputs AST trees in an extended JSON format called "Yojson".

- The precise AST datatype is described using the "ATD" language. Most of the definitions are embedded in the c++ code of the ASTExporter.

- We use scripts in libtooling/atdlib to extract and process the ATD definitions, then we use atdgen to generate the ocaml type definitions and json stub.

- The main program clang_ast_yojson_validator.ml is meant to parse, re-print, and compare yojson files emitted by ASTExporter.
  We use ydump (part of the yojson package) to normalize the original json and the re-emitted json before comparing them.

http://mjambon.com/atdgen/atdgen-manual.html
http://mjambon.com/yojson.html
