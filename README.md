Facebook Clang Plugins
======================

This [repository](https://github.com/facebook/facebook-clang-plugins) aims to share some useful clang plugins developed at Facebook.

It contains two kinds of plugins to the [clang compiler](http://clang.llvm.org/):

- analyzer plugins use the internal APIs of [clang analyzer](http://clang-analyzer.llvm.org/) to find bugs and report them;

- frontend plugins process the syntax of source files directly to accomplish more general tasks; specifically, we have developed a clang-to-ocaml bridge to make code analyses easier.

Most of the plugins here have been written with iOS in mind. However different platforms may be considered in the future.

Pre-release notes
-----------------

This pre-release is meant to spark interest and gather early feedback.
Plugins are still subject to be deleted, moved, added, or heavily rewritten.

Structure of the repository
---------------------------

- [`analyzer`](https://github.com/facebook/facebook-clang-plugins/tree/master/analyzer) : plugins for clang analyzer,

- [`libtooling`](https://github.com/facebook/facebook-clang-plugins/tree/master/libtooling) : frontend plugins (currently a clang-to-json AST exporter),

- [`clang-ocaml`](https://github.com/facebook/facebook-clang-plugins/tree/master/clang-ocaml) : ocaml libraries to process the Json output of frontend plugins,

- [`extra-repo-example`](https://github.com/facebook/facebook-clang-plugins/tree/master/extra-repo-example) : example of external repository where to add plugins and piggyback on the build system.


Quick start
-----------

Clang plugins needs to be loaded in a target compiler that matches the API in use.

General instructions to compile clang can be found here: http://clang.llvm.org/get_started.html

Compile clang with a setup script:
 To compile and use the required version of clang, please run ./clang/setup.sh. 
If you use this script, you don't need to export the variable  CLANG_PREFIX to compile the plugin.

Alternatively, here are the steps to do this manually:

The current version of the plugins requires a clang compiler to be compiled from the following sources:

- `llvm` http://llvm.org/git/llvm.git dcc0e7eaa12e0005a8eb8a92d1500129dced6153
- `llvm/tools/clang` http://llvm.org/git/clang.git 37e48be46c18b9322ff88daca6c096d86bd8e619
- `llvm/projects/compiler-rt` http://llvm.org/git/compiler-rt.git 3451762a4db1036b0576cbaa9d1a1309b981a634
- `llvm/projects/libcxx` http://llvm.org/git/libcxx.git 7ba3c57565e6658d8265b028a61c5731cf899495

Typically, compilation is made from a directory next to `llvm` along the following lines:
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

Caveat:
- Because of the nature of C++, clang and the plugins need to be compiled with the exact same C++ libraries.
- Also, the default stripping command of clang in release mode breaks plugins.

Once the target compiler is installed, the following should run the unit tests:
```
export CLANG_PREFIX=/usr/local #should be the same as above
export CLANG_PLUGINS_EXTRA_REPO=extra-repo-example
make test
```

Ocaml users may also run:
```
make -C clang-ocaml test  #requires proper ocaml libraries, see included clang-ocaml/README
```

Mac users may create an Xcode project as follows:
```
export CLANG_PREFIX=/usr/local #should be the same as above
make xcode
```

Additional configuration options are available in `Makefile.config` (and possibly `CMakeLists.txt`).
