#!/bin/bash
set -x

# Simple installation script for llvm/clang.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_SRC="$REPO_ROOT/clang/src/clang-3.6dev.tgz"
CLANG_PREFIX="$REPO_ROOT/clang"

platform=`uname`

if [ $platform == 'Darwin' ]; then
    CONFIGURE_ARGS=(
        --prefix="$CLANG_PREFIX"
        --enable-libcpp
        --enable-cxx11
        --disable-assertions
        --enable-optimized
    )
elif [ $platform == 'Linux' ]; then
    CONFIGURE_ARGS=(
        --prefix="$CLANG_PREFIX"
        --enable-cxx11
        --disable-assertions
        --enable-optimized
    )
else
    echo "Clang setup: platform $platform is currently not supported by this script"; exit 1
fi

# start the installation
echo "Installing clang..."
TMP=`mktemp -d /tmp/clang-setup.XXXXXX`
pushd "$TMP"
tar xzf "$CLANG_SRC"
llvm/configure "${CONFIGURE_ARGS[@]}"

mkdir -p "$CLANG_PREFIX"
JOBS=8
if [ -f /proc/cpuinfo ]; then
       JOBS=`grep -c processor /proc/cpuinfo`
fi
make -j $JOBS && make install
cp Release/bin/clang "$CLANG_PREFIX/bin/clang"
strip -x "$CLANG_PREFIX/bin/clang"
popd

rm -rf "$TMP"
