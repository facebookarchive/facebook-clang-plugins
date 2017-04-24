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
#include <clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>

#include "DanglingDelegateCommon.h"

using namespace clang;
using namespace ento;

namespace DanglingDelegate {

const ObjCInterfaceDecl *getCurrentTopClassInterface(
    const CheckerContext &context) {
  const StackFrameContext *sfc = context.getStackFrame();
  while (sfc && !sfc->inTopFrame()) {
    sfc = sfc->getParent()->getCurrentStackFrame();
  }
  if (!sfc) {
    return NULL;
  }
  const ObjCMethodDecl *md = dyn_cast_or_null<ObjCMethodDecl>(sfc->getDecl());
  if (!md) {
    return NULL;
  }
  return md->getClassInterface();
}

// strips expressions a bit more than IgnoreParenImpCasts
const Expr *ignoreOpaqueValParenImpCasts(const Expr *expr) {
  if (!expr) {
    return NULL;
  }
  const Expr *E = expr;
  do {
    expr = E;
    E = E->IgnoreParenImpCasts();
    const OpaqueValueExpr *ove = dyn_cast_or_null<OpaqueValueExpr>(E);
    if (ove) {
      E = ove->getSourceExpr();
    }
  } while (E != NULL && E != expr);

  return E;
}

bool isObjCSelfExpr(const Expr *expr) {
  expr = ignoreOpaqueValParenImpCasts(expr);
  if (!expr) {
    return false;
  }
  return expr->isObjCSelfExpr();
}

const Expr *getArgOfObjCMessageExpr(const ObjCMessageExpr &expr,
                                    unsigned index) {
  if (expr.getNumArgs() < index + 1) {
    return NULL;
  }
  return expr.getArg(index);
}

const std::string getPropertyNameFromSetterSelector(const Selector &selector) {
  std::string selectorStr = selector.getAsString();
  // fancy names for setters are not supported
  if (selectorStr.size() < 5 || !StringRef(selectorStr).startswith("set")) {
    return "";
  }
  std::string propName(selectorStr.begin() + 3, selectorStr.end() - 1);
  char x = propName[0];
  propName[0] = (x >= 'A' && x <= 'Z' ? x - 'A' + 'a' : x);
  return propName;
}

const ObjCPropertyDecl *matchObjCMessageWithPropertyGetter(
    const ObjCMessageExpr &expr) {
  const ObjCMethodDecl *methDecl = expr.getMethodDecl();
  if (!methDecl) {
    return NULL;
  }

  if (!methDecl->isPropertyAccessor() || methDecl->param_size() != 0) {
    return NULL;
  }
  const ObjCInterfaceDecl *intDecl = expr.getReceiverInterface();
  if (!intDecl) {
    return NULL;
  }

  std::string propName = expr.getSelector().getAsString();
  IdentifierInfo &ii = intDecl->getASTContext().Idents.get(propName);
    return intDecl->FindPropertyDeclaration(&ii, ObjCPropertyQueryKind::OBJC_PR_query_unknown);
}

const ObjCPropertyDecl *matchObjCMessageWithPropertySetter(
    const ObjCMessageExpr &expr) {
  const ObjCMethodDecl *methDecl = expr.getMethodDecl();
  if (!methDecl) {
    return NULL;
  }

  if (!methDecl->isPropertyAccessor() || methDecl->param_size() != 1) {
    return NULL;
  }
  const ObjCInterfaceDecl *intDecl = expr.getReceiverInterface();
  if (!intDecl) {
    return NULL;
  }

  Selector selector = expr.getSelector();
  std::string propName = getPropertyNameFromSetterSelector(selector);
  if (propName.empty()) {
    return NULL;
  }
  IdentifierInfo &II = intDecl->getASTContext().Idents.get(propName);
  return intDecl->FindPropertyDeclaration(&II, ObjCPropertyQueryKind::OBJC_PR_query_unknown);
}

// matches expressions _x and self.x
const ObjCIvarDecl *matchIvarLValueExpression(const Expr &expr) {
  const Expr *normExpr = ignoreOpaqueValParenImpCasts(&expr);
  if (!normExpr) {
    return NULL;
  }
  // ivar?
  const ObjCIvarRefExpr *ivarRef = dyn_cast<ObjCIvarRefExpr>(normExpr);
  if (ivarRef) {
    return ivarRef->getDecl();
  }

  // getter on self?
  const PseudoObjectExpr *poe = dyn_cast<PseudoObjectExpr>(normExpr);
  if (!poe) {
    return NULL;
  }
  const ObjCPropertyRefExpr *pre =
      dyn_cast_or_null<ObjCPropertyRefExpr>(poe->getSyntacticForm());
  if (!pre) {
    return NULL;
  }
  // we want a getter corresponding to a real ivar of the current class
  if (!pre->isMessagingGetter() || pre->isImplicitProperty()) {
    return NULL;
  }
  const Expr *base = ignoreOpaqueValParenImpCasts(pre->getBase());
  if (!base || !base->isObjCSelfExpr()) {
    return NULL;
  }

  ObjCPropertyDecl *propDecl = pre->getExplicitProperty();
  if (!propDecl) {
    return NULL;
  }
  return propDecl->getPropertyIvarDecl();
}

// matches [NSNotificationCenter defaultCenter] etc and make a string out of it
const std::string matchObservableSingletonObject(const Expr &expr) {
  const ObjCMessageExpr *msg =
      dyn_cast_or_null<ObjCMessageExpr>(ignoreOpaqueValParenImpCasts(&expr));
  if (!msg || msg->getReceiverKind() != ObjCMessageExpr::Class ||
      !msg->getReceiverInterface()) {
    return "";
  }

  std::string intName = msg->getReceiverInterface()->getNameAsString();
  std::string selectorStr = msg->getSelector().getAsString();
  if (intName == "NSNotificationCenter" && selectorStr == "defaultCenter") {
    return "+[" + intName + " " + selectorStr + "]";
  }

  return "";
}

bool isKnownToBeNil(const clang::ento::SVal &sVal,
                    clang::ento::CheckerContext &context) {
  Optional<DefinedSVal> dv = sVal.getAs<DefinedSVal>();
  if (dv) {
    ConstraintManager &manager = context.getConstraintManager();
    ProgramStateRef stateNotNull =
        manager.assume(context.getState(), *dv, true);
    if (!stateNotNull) {
      // the value cannot be "not null"
      return true;
    }
  }
  return false;
}

} // end of namespace
