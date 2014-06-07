Facebook clang plugins
======================

Creating tools for languages of the C family is notoriously hard and subject to the evolution of languages. Fortunately, the [clang compiler](http://clang.llvm.org/) makes available rich language APIs and a convenient plugin system to use them.

The purpose of this [project](https://github.com/facebook/facebook-clang-plugins) is to share some useful clang plugins developped at Facebook.

In general, clang plugins come in two "kinds":

- analyzer plugins use the internal APIs of [clang analyzer](http://clang-analyzer.llvm.org/) to find bugs and report them; those can also be seen as "linting rules";

- frontend plugins make it possible to process the syntax of source files exactly as clang sees it to accomplish various tasks.

Most of the plugins here have been written to improve the experience of engineers working on iOS. However different platforms could be considered in the future.


Directory tree
--------------

- analyzer -> plugins for clang analyzer
- libtooling -> frontend plugins
- clang-ocaml -> an Ocaml library to import the AST of clang as serialized by the frontend plugin Yojson.
- extra-repo-example -> example of external repository where to add plugins

(generated)
- xcode -> Xcode project
- */build -> build products

Quickstart
----------

Edit Makefile.config (and possibly CMakeLists.txt) to set the different PATH variables to the target clang compiler.

The target clang compiler should be compiled from the following source directories:
-  llvm                      -> http://llvm.org/git/llvm.git 070b5745aef302b3d391840eb323ad6a3c5aa9e6
-  llvm/tools/clang          -> http://llvm.org/git/clang.git 182109a097267d9d2e90fa6235e4da4a09f86ad8
-  llvm/projects/compiler-rt -> http://llvm.org/git/compiler-rt.git 4819e32d7d9862dec08ed171765713b3cb40fdbf
-  llvm/projects/libcxx      -> http://llvm.org/git/libcxx.git a2df82b98e2e55019180b0c8de88211954de0646

Caveat: Plugins heavily depend on clang internals: using sources with distant commit numbers is likely to break.

Typically, compilations are made from a directory next to llvm:
```
# from $HOME/git or equivalent
export CLANG_PREFIX=/usr/local
mkdir -p llvm-build
cd llvm-build
../llvm/configure --prefix="$CLANG_PREFIX" --enable-libcpp --enable-cxx11 --disable-assertions --enable-optimized
make -j 8 && make install
cp Release/bin/clang "$CLANG_PREFIX/bin/clang"
strip -x "$CLANG_PREFIX/bin/clang"
```

Then, the following should run the unit tests:
```
export CLANG_PREFIX=/usr/local #should be the same as above
export CLANG_PLUGINS_EXTRA_REPO=extra-repo-example
make test
```

Ocaml users may also run:
```
make -C clang-ocaml test  #requires proper ocaml libraries, see included clang-ocaml/README
```

Mac users may create an xcode project as follows:
```
export CLANG_PREFIX=/usr/local #should be the same as above
make xcode
```
