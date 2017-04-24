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

#include "DanglingDelegateFactFinder.h"
#include "DanglingDelegateCommon.h"

using namespace clang;
using namespace ento;

namespace DanglingDelegate {

void FactFinder::ObjCImplFacts::ivarMayStoreSelfInUnsafeProperty(
    const clang::ObjCIvarDecl *ivarDecl, const std::string &propName) {
  struct IvarFacts &ivarFacts = _ivarFactsMap[ivarDecl];
  ivarFacts._mayStoreSelfInUnsafeProperty.insert(propName);
}

void FactFinder::ObjCImplFacts::ivarMayTargetSelf(
    const clang::ObjCIvarDecl *ivarDecl) {
  struct IvarFacts &ivarFacts = _ivarFactsMap[ivarDecl];
  ivarFacts._mayTargetSelf = true;
}

void FactFinder::ObjCImplFacts::ivarMayObserveSelf(
    const clang::ObjCIvarDecl *ivarDecl) {
  struct IvarFacts &ivarFacts = _ivarFactsMap[ivarDecl];
  ivarFacts._mayObserveSelf = true;
}

void FactFinder::ObjCImplFacts::sharedObjectMayObserveSelfInMethod(
    std::string objectName, const std::string &methodName) {
  struct SharedObserverFacts &shareObserverFacts =
      _sharedObserverFactsMap[objectName];
  shareObserverFacts._mayAddObserverToSelfInObjCMethod.insert(methodName);
}

// pre-process method declarations to detect pseudo-init methods and dealloc
void FactFinder::VisitMethodDecl() {

  // needs to be lowercase
  static std::string pseudoInitPrefixes[] = {"_init",
                                             "setup",
                                             "_setup",
                                             "load",
                                             "_load",
                                             "viewdidload",
                                             "_viewdidload"};

  assert(_methDecl);
  std::string methName = _methDecl->getNameAsString();
  if (_methDecl->getMethodFamily() == OMF_init) {
    _facts._pseudoInitMethods.insert(methName);
    return;
  }

  if (methName == "dealloc") {
    _facts._hasDealloc = true;
  }

  for (unsigned i = 0;
       i < sizeof(pseudoInitPrefixes) / sizeof(pseudoInitPrefixes[0]);
       i++) {
    if (StringRef(methName).startswith_lower(pseudoInitPrefixes[i])) {
      _facts._pseudoInitMethods.insert(methName);
      return;
    }
  }
}

void FactFinder::VisitChildren(const Stmt *stmt) {
  assert(stmt);
  for (auto it = stmt->child_begin(), end = stmt->child_end(); it != end;
       ++it) {
    if (const Stmt *child = *it) {
      Visit(child);
    }
  }
}

// try to detect creation of unsafe references to self in the general
// (non-setter) case
void FactFinder::VisitObjCMessageExpr(const ObjCMessageExpr *expr) {

  const Expr *receiver = expr->getInstanceReceiver();
  if (!receiver) {
    VisitChildren(expr);
    return;
  }

  // we expect self to be the first argument in all cases
  if (!isObjCSelfExpr(getArgOfObjCMessageExpr(*expr, 0))) {
    VisitChildren(expr);
    return;
  }

  std::string selectorStr = expr->getSelector().getAsString();

  // CASE 1: is the receiver an ivar?
  const ObjCIvarDecl *ivarDecl = matchIvarLValueExpression(*receiver);
  if (ivarDecl) {
    // try to find an assign property we may be storing self into
    const ObjCPropertyDecl *propDecl =
        matchObjCMessageWithPropertySetter(*expr);
    if (propDecl && propDecl->getSetterKind() == ObjCPropertyDecl::Assign) {
      _facts.ivarMayStoreSelfInUnsafeProperty(ivarDecl,
                                              propDecl->getNameAsString());
    } else if (StringRef(selectorStr).startswith("addTarget:")) {
      _facts.ivarMayTargetSelf(ivarDecl);
    } else if (StringRef(selectorStr).startswith("addObserver:")) {
      _facts.ivarMayObserveSelf(ivarDecl);
    }

    VisitChildren(expr);
    return;
  }

  // CASE 2: is the receiver an interesting "observable singleton object"?
  std::string receiverObjectName = matchObservableSingletonObject(*receiver);
  if (receiverObjectName != "") {

    if (StringRef(selectorStr).startswith("addObserver:")) {
      _facts.sharedObjectMayObserveSelfInMethod(receiverObjectName,
                                                _methDecl->getNameAsString());
    }
  }

  VisitChildren(expr);
}

} // end of namespace
