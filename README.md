Facebook Clang Plugins
======================

This [repository](https://github.com/facebook/facebook-clang-plugins) aims to share some useful clang plugins developed at Facebook.

It contains two kinds of plugins to the [clang compiler](http://clang.llvm.org/):

- analyzer plugins use the internal APIs of [clang analyzer](http://clang-analyzer.llvm.org/) to find bugs and report them;

- frontend plugins process the syntax of source files directly to accomplish more general tasks; specifically, we have developed a clang-to-ocaml bridge to make code analyses easier.

Most of the plugins here have been written with iOS in mind. However, different platforms may be considered in the future.

Pre-release notes
-----------------

This pre-release is meant to spark interest and gather early feedback.
Plugins are still subject to be deleted, moved, added, or heavily rewritten.

Structure of the repository
---------------------------

- [`analyzer`](https://github.com/facebook/facebook-clang-plugins/tree/master/analyzer) : plugins for clang analyzer,

- [`libtooling`](https://github.com/facebook/facebook-clang-plugins/tree/master/libtooling) : frontend plugins (currently a clang-to-json AST exporter),

- [`clang-ocaml`](https://github.com/facebook/facebook-clang-plugins/tree/master/clang-ocaml) : ocaml libraries to process the JSON output of frontend plugins,

- [`extra-repo-example`](https://github.com/facebook/facebook-clang-plugins/tree/master/extra-repo-example) : example of external repository where to add plugins and piggyback on the build system.


Quick start
-----------

The current version of the plugins requires the version 3.6.1 of the clang compiler, re-compiled from source.

General instructions to compile clang can be found here: http://clang.llvm.org/get_started.html

(a) Compiling clang from the included sources and script

To compile and use the required version of clang, please run ./clang/setup.sh.
Using this script should make the variable CLANG_PREFIX unnecessary to compile the plugin.

(b) Manual steps

We have tested the following git checkouts under Mac OS:
- `llvm` http://llvm.org/git/llvm.git 66ad281fb93b92858f443915f7811b16c131d3eb
- `llvm/tools/clang` http://llvm.org/git/clang.git 9fd69610a42900694945c75448efd04373359dff
   with Apple's patch for nullability annotations: http://lists.cs.uiuc.edu/pipermail/cfe-dev/2015-March/041798.html
- `llvm/projects/compiler-rt` http://llvm.org/git/compiler-rt.git 55a3690916df972339fe877e077a2f27c7117211
- `llvm/projects/libcxx` http://llvm.org/git/libcxx.git 3da9473a817576c6a94c3f8e0842f3210b0e8aa9

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
