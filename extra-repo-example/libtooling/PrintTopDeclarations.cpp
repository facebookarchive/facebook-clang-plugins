/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/Support/raw_ostream.h>

#include "SimplePluginASTAction.h"

using namespace clang;

namespace {

class PrintDeclarationsConsumer : public ASTConsumer {
private:
  llvm::raw_ostream &OS;

public:
  PrintDeclarationsConsumer(CompilerInstance &CI, llvm::StringRef InputFile, raw_ostream &OS) : OS(OS) {}

  virtual bool HandleTopLevelDecl(DeclGroupRef DG) {
    for (DeclGroupRef::iterator i = DG.begin(), e = DG.end(); i != e; ++i) {
      const Decl *D = *i;
      OS << D->getDeclKindName();
      if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
        OS << " " << ND->getNameAsString() << "\n";
    }

    return true;
  }
};

  typedef SimplePluginASTAction<PrintDeclarationsConsumer> PrintTopDeclarations;

}

static FrontendPluginRegistry::Add<PrintTopDeclarations>
X("PrintTopDeclarations", "Print top-level declarations");
