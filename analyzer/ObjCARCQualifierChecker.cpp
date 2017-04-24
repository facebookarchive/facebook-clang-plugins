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
 * Defines a checker to detect a common misuse of -[NSInvocation
 * getArgument:atIndex:] and -[NSInvocation getReturnValue:] under ARC.
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

using namespace clang;
using namespace ento;

namespace {
class WalkAST : public ConstStmtVisitor<WalkAST> {
  const CheckerBase &Checker;
  BugReporter &BR;
  AnalysisDeclContext *AC;

 public:
  WalkAST(const CheckerBase &Checker, BugReporter &br, AnalysisDeclContext *ac)
      : Checker(Checker), BR(br), AC(ac) {}

  // Statement visitor methods.
  void VisitChildren(const Stmt *S);
  void VisitStmt(const Stmt *S) { VisitChildren(S); }
  void VisitObjCMessageExpr(const ObjCMessageExpr *ME);
};
} // end anonymous namespace

void WalkAST::VisitObjCMessageExpr(const ObjCMessageExpr *ME) {
  if (!ME) {
    return;
  }

  const ObjCInterfaceDecl *ID = ME->getReceiverInterface();

  if (ID) {
    std::string receiverClassName = ID->getIdentifier()->getName();
    Selector sel = ME->getSelector();

    if (receiverClassName == "NSInvocation" &&
        (sel.getAsString() == "getArgument:atIndex:" ||
         sel.getAsString() == "getReturnValue:")) {

      const Expr *Arg = ME->getArg(0);
      if (!Arg) {
        VisitChildren(ME);
        return;
      }

      QualType T = Arg->IgnoreParenImpCasts()->getType();
      const PointerType *PT = T->getAs<PointerType>();
      if (!PT) {
        // there appears to be a type error anyway...
        VisitChildren(ME);
        return;
      }
      T = PT->getPointeeType();
      if (!T->isObjCRetainableType()) {
        VisitChildren(ME);
        return;
      }
      const AttributedType *AttrType = T->getAs<AttributedType>();
      bool passed = false;
      if (AttrType) {
        Qualifiers::ObjCLifetime argLifetime =
            AttrType->getModifiedType().getObjCLifetime();
        passed = argLifetime == Qualifiers::OCL_None ||
                 argLifetime == Qualifiers::OCL_ExplicitNone;
      }

      if (!passed) {
        SmallString<64> BufName, Buf;
        llvm::raw_svector_ostream OsName(BufName), Os(Buf);

        OsName << "Invalid use of '" << receiverClassName << "'";
        Os << "In ARC mode, the first argument to '-[" << receiverClassName
           << " " << sel.getAsString()
           << "]' must be declared with __unsafe_unretained to avoid "
              "over-release.";

        PathDiagnosticLocation MELoc =
            PathDiagnosticLocation::createBegin(ME, BR.getSourceManager(), AC);
        BR.EmitBasicReport(AC->getDecl(),
                           &Checker,
                           OsName.str(),
                           "Memory error (Facebook)",
                           Os.str(),
                           MELoc);
      }
    }
  }
  VisitChildren(ME);
}

void WalkAST::VisitChildren(const Stmt *S) {
  for (Stmt::const_child_iterator I = S->child_begin(), E = S->child_end();
       I != E;
       ++I)
    if (const Stmt *child = *I)
      Visit(child);
}

namespace {
class ObjCARCQualifierChecker : public Checker<check::ASTCodeBody> {
 public:
  void checkASTCodeBody(const Decl *D,
                        AnalysisManager &Mgr,
                        BugReporter &BR) const {

    if (D->getASTContext().getLangOpts().ObjCAutoRefCount) {
      WalkAST walker(*this, BR, Mgr.getAnalysisDeclContext(D));
      walker.Visit(D->getBody());
    }
  }
};
}

REGISTER_CHECKER_IN_PLUGIN(ObjCARCQualifierChecker,
                           "facebook.ObjCARCQualifierChecker",
                           "checker for missing ARC qualifiers.")
