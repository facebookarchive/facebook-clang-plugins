#!/bin/bash
# Simple installation script for llvm/clang.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_SRC="$REPO_ROOT/clang/src/clang-3.6dev.tgz"
CLANG_PREFIX="$REPO_ROOT/clang"

platform=`uname`

if [ $platform == 'Darwin' ]; then
    echo "Installing clang..."

    TMP=`mktemp -d /tmp/clang-setup.XXXXXX`
    cd "$TMP"

    tar xzf "$CLANG_SRC"
    llvm/configure --prefix="$CLANG_PREFIX" --enable-libcpp --enable-cxx11 --disable-assertions --enable-optimized

    mkdir -p "$CLANG_PREFIX"
    make -j 8 && make install
    cp Release/bin/clang "$CLANG_PREFIX/bin/clang"
    strip -x "$CLANG_PREFIX/bin/clang"
    
    rm -rf "$TMP"
else
    echo "This installation script currently supports only Mac OS X."
fi
