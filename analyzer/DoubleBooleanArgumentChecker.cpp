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
 * Defines a checker that detects simple bogus boolean expressions containing a same variable twice in a row, e.g x || x .
 */

#include "PluginMainRegistry.h"

#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>

using namespace clang;
using namespace ento;

namespace {
  
  class WalkAST : public ConstStmtVisitor<WalkAST> {
    const CheckerBase &Checker;
    BugReporter &BR;
    AnalysisDeclContext *AC;
    
  public:
    WalkAST(const CheckerBase &checker,
            BugReporter &br,
            AnalysisDeclContext *ac) : Checker(checker), BR(br), AC(ac) {}

    // Statement visitor methods.
    void VisitChildren(const Stmt *S);
    void VisitStmt(const Stmt *S) { VisitChildren(S); }
    void VisitBinaryOperator(const BinaryOperator *B);
  };
} // end anonymous namespace

void WalkAST::VisitChildren(const Stmt *S) {
  for (Stmt::const_child_iterator I = S->child_begin(), E = S->child_end(); I != E; ++I)
    if (const Stmt *child = *I)
      Visit(child);
}

void WalkAST::VisitBinaryOperator(const BinaryOperator *B) {
  if (B && (B->isBitwiseOp() || B->isLogicalOp())) {
    const Expr *LHS = B->getLHS();
    const Expr *RHS = B->getRHS();
    if (LHS && RHS) {
      LHS = LHS->IgnoreImpCasts();
      RHS = RHS->IgnoreImpCasts();
      const DeclRefExpr *LDR = dyn_cast<DeclRefExpr>(LHS);
      const DeclRefExpr *RDR = dyn_cast<DeclRefExpr>(RHS);
      if (LDR && RDR) {
        // Check to see if this is a named variable.
        const VarDecl *LVar = dyn_cast<VarDecl>(LDR->getDecl());
        const VarDecl *RVar = dyn_cast<VarDecl>(RDR->getDecl());
        if (LVar && RVar) {
          std::string LName = LVar->getQualifiedNameAsString();
          std::string RName = RVar->getQualifiedNameAsString();
          if (LName == RName) {
            SmallString<64> BufName, Buf;
            llvm::raw_svector_ostream OsName(BufName), Os(Buf);
            Os << "Boolean expression contains argument "
            << LName << " more than once.";
            PathDiagnosticLocation MELoc =
            PathDiagnosticLocation::createBegin(B, BR.getSourceManager(), AC);
            BR.EmitBasicReport(AC->getDecl(),
                               &Checker,
                               OsName.str(),
                               "API error (Facebook)",
                               Os.str(),
                               MELoc);
          }
        }
      }
    }
  }
}

namespace {
  class DoubleBooleanArgumentChecker  : public Checker<check::ASTCodeBody> {
  public:
    void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                          BugReporter &BR) const {
      WalkAST walker(*this, BR, Mgr.getAnalysisDeclContext(D));
      walker.Visit(D->getBody());
    }
  };
}

REGISTER_CHECKER_IN_PLUGIN(DoubleBooleanArgumentChecker,
                           "facebook.DoubleBooleanArgumentChecker",
                           "Check to guard against x && x statements.")
