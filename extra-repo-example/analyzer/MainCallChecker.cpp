/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>

#include "PluginMainRegistry.h"

using namespace clang;
using namespace ento;

namespace {
class MainCallChecker : public Checker < check::PreStmt<CallExpr> > {
  mutable std::unique_ptr<BugType> BT;

public:
  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;
};

} // end anonymous namespace

void MainCallChecker::checkPreStmt(const CallExpr *CE, CheckerContext &C) const {
  const ProgramStateRef state = C.getState();
  const LocationContext *LC = C.getLocationContext();
  const Expr *Callee = CE->getCallee();
  const FunctionDecl *FD = state->getSVal(Callee, LC).getAsFunctionDecl();

  if (!FD)
    return;

  // Get the name of the callee.
  IdentifierInfo *II = FD->getIdentifier();
  if (!II)   // if no identifier, not a simple C function
    return;

  if (II->isStr("main")) {
    ExplodedNode *N = C.generateSink(state, C.getPredecessor());
    if (!N)
      return;

    if (!BT)
      BT.reset(new BugType(this,
                           "call to main",
                           "example bug"));

    auto report = llvm::make_unique<BugReport>(*BT, BT->getName(), N);
    report->addRange(Callee->getSourceRange());
    C.emitReport(std::move(report));
  }
}

REGISTER_CHECKER_IN_PLUGIN(MainCallChecker,
			   "example.MainCallChecker",
			   "Disallows calls to functions called main")
