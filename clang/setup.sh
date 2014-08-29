platform=`uname`

CLANG_PREFIX="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

#Install clang
if [ $platform == 'Darwin' ]; then
    TMP=`mktemp -d /tmp/clang.XXXXXX`
    
    # setup clang
    echo "installing clang..."
    cd "$TMP"
    cp  $CLANG_PREFIX/src/clang-3.5.tar.gz .
    tar xzf clang-3.5.tar.gz
    llvm/configure --prefix="$CLANG_PREFIX" --enable-libcpp --enable-cxx11 --disable-assertions --enable-optimized
    make -j 8 && make install
    cp Release/bin/clang "$CLANG_PREFIX/bin/clang"
    strip -x "$CLANG_PREFIX/bin/clang"
    
    rm -rf "$TMP"
else echo "This clang plugin is currently only supported in Mac OS."
fi
