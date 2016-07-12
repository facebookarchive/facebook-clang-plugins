/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * Defines a checker for warning against not declaring static/global variables
 * for dispatch_once_t.
 */

#include <clang/StaticAnalyzer/Core/BugReporter/BugReporter.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h>
#include <clang/StaticAnalyzer/Core/CheckerRegistry.h>
#include <clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h>
#include <clang/AST/Decl.h>
#include "PluginMainRegistry.h"

using namespace clang;
using namespace ento;

class GlobalOrStaticVariableForDispatchOnceChecker
    : public Checker<check::ASTDecl<VarDecl>,
                     check::ASTDecl<ObjCIvarDecl>,
                     check::ASTDecl<ObjCPropertyDecl>> {

 private:
  static bool isDispatchOnceType(const TypeSourceInfo *typeSourceInfo) {
    return typeSourceInfo &&
           (typeSourceInfo->getType().getAsString() == "dispatch_once_t");
  }

  void emitReport(const Decl *D, BugReporter &BR) const {
    PathDiagnosticLocation L =
        PathDiagnosticLocation::create(D, BR.getSourceManager());
    BR.EmitBasicReport(
        D,
        this,
        "Non-Global/Static variable for dispatch_once_t",
        "Semantic error (Facebook)",
        "Using an instance variable or local variable as the predicate passed to "
        "dispatch_once/dispatch_once_f causes undefined behavior. "
        "Please use a statically allocated dispatch_once_t instead",
        L);
  }

 public:
  void checkASTDecl(const VarDecl *D,
                    AnalysisManager &mgr,
                    BugReporter &BR) const {
    if (D && isDispatchOnceType(D->getTypeSourceInfo()) &&
        !D->hasGlobalStorage() && !D->isStaticLocal()) {
      this->emitReport(D, BR);
    }
  }

  void checkASTDecl(const ObjCIvarDecl *D,
                    AnalysisManager &mgr,
                    BugReporter &BR) const {
    if (D && isDispatchOnceType(D->getTypeSourceInfo())) {
      this->emitReport(D, BR);
    }
  }

  void checkASTDecl(const ObjCPropertyDecl *D,
                    AnalysisManager &mgr,
                    BugReporter &BR) const {
    if (D && isDispatchOnceType(D->getTypeSourceInfo())) {
      this->emitReport(D, BR);
    }
  }
};

REGISTER_CHECKER_IN_PLUGIN(
    GlobalOrStaticVariableForDispatchOnceChecker,
    "facebook.GlobalOrStaticVariableForDispatchOnceChecker",
    "Check for non static/global vars in dispatch_once_t")
