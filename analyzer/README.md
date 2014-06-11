
Checker plugins for Clang analyzer
----------------------------------

Assuming that the current dir is the root of the git repository and CLANG_PREFIX=/usr/local, you may compile and run tests with
```
export CLANG_PREFIX=/usr/local
make -C analyzer test
```

iOS tests will skipped if SDKPATH is unset (see Makefile.config).

See http://clang-analyzer.llvm.org/checker_dev_manual.html for general documentation.


How to debug
------------

As explained here: http://clang-analyzer.llvm.org/checker_dev_manual.html#commands, clang's general option --analyze does not do well with gdb.
You have to use -cc1 -analyze and give the actual front end options.

Fortunately this command line is included in crash logs. It should look like something as this:

```
$CLANG_PREFIX/bin/clang -cc1 -triple i386-apple-ios5.0.0 -analyze -disable-free -disable-llvm-verifier -main-file-name    \
  MainCallCheckerTest_BAD.m -analyzer-store=region -analyzer-opt-analyze-nested-blocks -analyzer-eagerly-assume           \
  -analyzer-checker=core -analyzer-checker=unix -analyzer-checker=osx -analyzer-checker=deadcode                          \
  -analyzer-checker=security.insecureAPI.UncheckedReturn -analyzer-checker=security.insecureAPI.getpw                     \
  -analyzer-checker=security.insecureAPI.gets -analyzer-checker=security.insecureAPI.mktemp                               \
  -analyzer-checker=security.insecureAPI.mkstemp -analyzer-checker=security.insecureAPI.vfork -analyzer-output plist -w   \
  -mrelocation-model pic -pic-level 2 -mdisable-fp-elim -masm-verbose -target-cpu yonah -target-linker-version 136        \
  -gdwarf-2 -resource-dir $CLANG_PREFIX/bin/clang/bin/../lib/clang/3.4 -isysroot                                          \
  /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator6.1.sdk -O0 \
  -std=gnu99 -fdebug-compilation-dir clang/analyzer -ferror-limit 19 -fmessage-length 0 -stack-protector 1 -mstackrealign \
  -fblocks -fobjc-runtime=ios-5.0.0 -fobjc-default-synthesize-properties -fencode-extended-block-signature                \
  -fobjc-exceptions -fexceptions -fpascal-strings -fdiagnostics-show-option -fasm-blocks -vectorize-loops -load           \
  build/FacebookAnalyzerPlugin.dylib -analyzer-checker=example.MainCallChecker                                            \
  MainCallCheckerTest_BAD.plist -x objective-c tests/MainCallCheckerTest_BAD.m
```
