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
 * Define a checker to give a warning when a call to respondsToSelector
 * guards an access to a different selector.
 * Calls via instance methods are considered OK.
 *
 * Example:
 * if ([receiver respondsToSelector:@selector(someSel:)])
 *   { [receiver otherSel:3]; // might not respond to 'otherSel' }
 */

#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>
#include "clang/AST/StmtVisitor.h"
#include "PluginMainRegistry.h"

using namespace clang;
using namespace ento;

namespace {
// Stmt walker class: visit all the sub-statements recursively and check If
// statements
class WalkAST : public ConstStmtVisitor<WalkAST> {
 private:
  const CheckerBase &_checker;
  BugReporter &_br;
  AnalysisDeclContext *_ac;
  const Decl *_decl;

 public:
  WalkAST(const CheckerBase &checker,
          BugReporter &br,
          AnalysisDeclContext *ac,
          const Decl *decl)
      : _checker(checker), _br(br), _ac(ac), _decl(decl) {}

  // Check an If statement: this is where the main logic of the checker is
  // implemented.
  void checkIfStmt(const IfStmt &ifStmt) const;

  // Statement visitor methods.

  // Visitor for generic statements: standard implementation.
  void VisitStmt(const Stmt *stmt) {
    if (stmt) {
      visitChildren(*stmt);
    }
  }

  // Visitor for If statements: specific to this checker.
  // On If statements, this gets called instead of the generic VisitStmt.
  void VisitIfStmt(const IfStmt *ifStmt) {
    if (ifStmt) {
      checkIfStmt(*ifStmt);
      visitChildren(*ifStmt);
    }
  }

  // Auxiliary function to visit the children of Stmt: standard implementation.
  // Change this if a shallow or partial recursive visit is required.
  void visitChildren(const Stmt &stmt) {
    for (auto it = stmt.child_begin(), end = stmt.child_end(); it != end;
         ++it) {
      if (*it) {
        Visit(*it);
      }
    }
  }

 private:
  // Report the warning.
  void reportWarning(const ObjCMessageExpr &msgExpr,
                     const ValueDecl &receiver,
                     const Selector &sel1,
                     const Selector &sel2) const;

  // Check a statement in the body of the 'then' branch until the first
  // candidate call is found.
  bool checkThenBodyStmt(const Stmt *thenBodyStmt,
                         const Expr &condReceiver,
                         const Selector &condSel) const;

  // Check a toplevel statement in the body of the 'then' branch.
  void checkToplevelThenBodyStmt(const Stmt &thenBodyS,
                                 const Expr &condReceiver,
                                 const Selector &condSel) const;
};

// Check if a selector is respondsToSelector:.
static bool isRespondsToSelector(const Selector &sel) {
  if (sel.getAsString() != "respondsToSelector:") {
    return false;
  }
  if (sel.getNumArgs() != 1) {
    assert(false); // Check Objective C convention that respondsToSelector has 1
                   // argument.
    return false;
  }
  return true;
}

// Check if a selector is of the family performSelector:_parameters_.
static bool isPerformSelector(const Selector &sel) {
  if (sel.getNameForSlot(0) != "performSelector") {
    return false;
  }
  return true;
}

// If an expression refers to a value, return a reference to its declaration,
// otherwise NULL.
static const ValueDecl *exprToValueDecl(const Expr &expr) {
  const Expr *exprNorm = expr.IgnoreParenImpCasts();
  if (const DeclRefExpr *declRefExpr = dyn_cast<DeclRefExpr>(exprNorm)) {
    return declRefExpr->getDecl();
  } else {
    return NULL;
  }
}

// Check if two expressions refer to the same value declaration.
static bool exprEqualValueDecl(const Expr &expr1, const Expr &expr2) {
  const ValueDecl *valueDecl1 = exprToValueDecl(expr1);
  const ValueDecl *valueDecl2 = exprToValueDecl(expr2);
  return valueDecl1 && valueDecl2 && (valueDecl1 == valueDecl2);
}

// Check if a message expression calls an existing instance method.
static bool msgExprInstanceMethodExists(const ObjCMessageExpr &msgExpr,
                                        const Selector &sel) {
  if (const ObjCInterfaceDecl *intDecl = msgExpr.getReceiverInterface()) {
    return (intDecl->lookupInstanceMethod(sel)) != NULL;
  } else {
    return false;
  }
}

// Get a normalized selector from a message expression by stripping
// 'performSelector' if present.
static const Selector getNormalizedSelector(const ObjCMessageExpr &msgExpr) {
  const Selector &sel = msgExpr.getSelector();
  if (isPerformSelector(sel)) {
    const Expr *selArgument = msgExpr.getArg(0);
    if (const ObjCSelectorExpr *selExpr =
            dyn_cast_or_null<ObjCSelectorExpr>(selArgument)) {
      return selExpr->getSelector();
    }
  }

  return sel;
}

void WalkAST::reportWarning(const ObjCMessageExpr &msgExpr,
                            const ValueDecl &receiver,
                            const Selector &sel1,
                            const Selector &sel2) const {
  auto bugName = "Suspicious use of respondsToSelector";
  auto bugCategory = "Semantic issue (Facebook)";

  llvm::SmallString<128> buf;
  llvm::raw_svector_ostream os(buf);
  os << "Suspicious use of respondsToSelector. A condition checks if "
     << receiver;
  ;
  os << " responds to selector " << sel1;
  os << " and subsequently selector " << sel2;
  os << " is used. The selector name might have been misspelled.";
  auto bugStr = os.str();

  auto pdl = new PathDiagnosticLocation(&msgExpr, _br.getSourceManager(), _ac);
  _br.EmitBasicReport(_decl, &_checker, bugName, bugCategory, bugStr, *pdl);
}

// Check statements using contexts from the grammar:
//   C ::= [X msg:Y] | foo(C,...C) | op C | C op C | return C | type var = C
// and stop at the first candidate method call found.
bool WalkAST::checkThenBodyStmt(const Stmt *thenBodyStmt,
                                const Expr &condReceiver,
                                const Selector &condSel) const {

  if (!thenBodyStmt) {
    return false;
  }

  // Normalize expressions.
  if (const Expr *expr = dyn_cast<Expr>(thenBodyStmt)) {
    thenBodyStmt = expr->IgnoreParenImpCasts();
  }

  if (const ObjCMessageExpr *thenBodyMsgExpr =
          dyn_cast<ObjCMessageExpr>(thenBodyStmt)) {
    const Selector &thenBodySel = getNormalizedSelector(*thenBodyMsgExpr);
    const Expr *thenBodyRec = thenBodyMsgExpr->getInstanceReceiver();
    if (!(thenBodyRec && exprEqualValueDecl(*thenBodyRec, condReceiver))) {
      return false;
    }

    bool isSelKnown =
        (thenBodySel == condSel) ||
        msgExprInstanceMethodExists(*thenBodyMsgExpr, thenBodySel);
    const ValueDecl *valueDecl = exprToValueDecl(condReceiver);
    if (valueDecl && !isSelKnown) {
      reportWarning(*thenBodyMsgExpr, *valueDecl, condSel, thenBodySel);
    }
    return true;
  } else if (const CallExpr *callExpr = dyn_cast<CallExpr>(thenBodyStmt)) {
    int nArgs = callExpr->getNumArgs();
    for (int i = 0; i < nArgs; i++) {
      if (checkThenBodyStmt(callExpr->getArg(i), condReceiver, condSel)) {
        return true;
      }
    }
  } else if (const UnaryOperator *unOp =
                 dyn_cast<UnaryOperator>(thenBodyStmt)) {
    if (checkThenBodyStmt(unOp->getSubExpr(), condReceiver, condSel))
      return true;
  } else if (const BinaryOperator *binOp =
                 dyn_cast<BinaryOperator>(thenBodyStmt)) {
    if (checkThenBodyStmt(binOp->getLHS(), condReceiver, condSel))
      return true;
    if (checkThenBodyStmt(binOp->getRHS(), condReceiver, condSel))
      return true;
  } else if (const ReturnStmt *retStmt = dyn_cast<ReturnStmt>(thenBodyStmt)) {
    if (const Expr *retExpr = retStmt->getRetValue()) {
      if (checkThenBodyStmt(retExpr, condReceiver, condSel))
        return true;
    }
  } else if (const DeclStmt *declStmt = dyn_cast<DeclStmt>(thenBodyStmt)) {
    if (const VarDecl *varDecl =
            dyn_cast_or_null<VarDecl>(declStmt->getSingleDecl())) {
      if (checkThenBodyStmt(
              varDecl->getAnyInitializer(), condReceiver, condSel))
        return true;
    }
  }
  return false;
}

// Check the immediate children of the 'then' body statement.
void WalkAST::checkToplevelThenBodyStmt(const Stmt &thenStmt,
                                        const Expr &condReceiver,
                                        const Selector &condSel) const {
  for (auto it = thenStmt.child_begin(), end = thenStmt.child_end(); it != end;
       ++it) {
    if (*it) {
      if (checkThenBodyStmt(*it, condReceiver, condSel))
        return;
    }
  }
}

// Match the pattern: If ([X respondsToSelector:Y]) { ... }
// and check every toplevel statement in the 'then' branch.
void WalkAST::checkIfStmt(const IfStmt &ifStmt) const {
  const Expr *cond = ifStmt.getCond(); // condition of the If statement
  const ObjCMessageExpr *condMsgExpr = dyn_cast_or_null<ObjCMessageExpr>(cond);
  if (condMsgExpr && isRespondsToSelector(condMsgExpr->getSelector())) {
    // TUNEME: might consider more complex conditions when tuning the checker.
    const Expr *condReceiver = condMsgExpr->getInstanceReceiver();
    if (!condReceiver)
      return;
    const Expr *condArgument = condMsgExpr->getArg(0);
    if (const ObjCSelectorExpr *condSelExpr =
            dyn_cast_or_null<ObjCSelectorExpr>(condArgument)) {
      const Selector &condSel = condSelExpr->getSelector();
      if (const Stmt *thenStmt = ifStmt.getThen()) {
        checkToplevelThenBodyStmt(*thenStmt, *condReceiver, condSel);
      }
    }
  }
}
} // end anonymous namespace

class SuspiciousRespondsToSelectorChecker : public Checker<check::ASTCodeBody> {
 public:
  // Called on every method in the AST.
  void checkASTCodeBody(const Decl *decl,
                        AnalysisManager &am,
                        BugReporter &br) const {
    if (AnalysisDeclContext *ac = am.getAnalysisDeclContext(decl)) {
      WalkAST walker(*this, br, ac, decl);
      walker.Visit(decl->getBody());
    }
  }
};

REGISTER_CHECKER_IN_PLUGIN(SuspiciousRespondsToSelectorChecker,
                           "facebook.SuspiciousRespondsToSelectorChecker",
                           "Check suspicious uses of respondsToSelector")
