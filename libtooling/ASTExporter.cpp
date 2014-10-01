/**
 * Copyright (c) 2014, Facebook, Inc.
 * Copyright (c) 2003-2014 University of Illinois at Urbana-Champaign.
 * All rights reserved.
 *
 * This file is distributed under the University of Illinois Open Source License.
 * See LLVM-LICENSE for details.
 *
 */

/**
 * Clang frontend plugin to export an AST of clang into Json and Yojson (and ultimately Biniou)
 * while conforming to the inlined ATD specifications.
 */

#include "ASTExporter.h"

//===----------------------------------------------------------------------===//
// ASTExporter Plugin Main
//===----------------------------------------------------------------------===//

namespace {

  using namespace ASTLib;

  template <class ATDWriter>
  class ExporterASTConsumer : public ASTConsumer {
  private:
    std::string BasePath;
    std::string DeduplicationServicePath;
    raw_ostream &OS;

  public:
    ExporterASTConsumer(CompilerInstance &CI,
                        StringRef InputFile,
                        StringRef BasePath,
                        StringRef DeduplicationServicePath,
                        raw_ostream &OS)
    : BasePath(BasePath),
      DeduplicationServicePath(DeduplicationServicePath),
      OS(OS)
    {}

    virtual void HandleTranslationUnit(ASTContext &Context) {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      FileUtils::DeduplicationService Dedup(DeduplicationServicePath, BasePath, Context.getSourceManager());
      ASTExporter<ATDWriter> P(OS, Context, BasePath, DeduplicationServicePath != "" ? &Dedup : nullptr);
      P.dumpDecl(D);
    }
  };

}

typedef SimplePluginASTAction<ExporterASTConsumer<JsonWriter>> JsonExporterASTAction;
typedef SimplePluginASTAction<ExporterASTConsumer<YojsonWriter>> YojsonExporterASTAction;

static FrontendPluginRegistry::Add<JsonExporterASTAction>
X("JsonASTExporter", "Export the AST of source files into ATD-specified Json data");

static FrontendPluginRegistry::Add<YojsonExporterASTAction>
Y("YojsonASTExporter", "Export the AST of source files into ATD-specified Yojson data");
