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
 * First syntactic pass for the "dangling delegate" checker.
 */

#pragma once

#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>
#include <clang/AST/StmtVisitor.h>

namespace DanglingDelegate {

typedef std::set<std::string> StringSet;

// Visitor class to gather facts on each ObjcImpl declaration
class FactFinder : public clang::ConstStmtVisitor<FactFinder> {

 public:
  // sub-structure recording facts about an ivar referencing self
  struct IvarFacts {

    StringSet _mayStoreSelfInUnsafeProperty; // e.g. we have seen [_x
                                             // setDelegate:self] in the code
                                             // where 'delegate' is an assign
                                             // property

    bool _mayTargetSelf; // we have seen [_x addTarget:self ..] in the code

    bool _mayObserveSelf; // we have seen [_x addObserver:self ..] in the code

    IvarFacts() : _mayTargetSelf(false), _mayObserveSelf(false) {}
  };

  // sub-structure recording facts about a global shared object referencing self
  struct SharedObserverFacts {

    StringSet _mayAddObserverToSelfInObjCMethod; // e.g. we have seen
                                                 // [[NSNotificationCenter
                                                 // defaultCenter]
                                                 // addObserver:self ..] in
                                                 // method 'init'

  };

  // structure recording facts over a class implementation
  struct ObjCImplFacts {

    typedef std::map<const clang::ObjCIvarDecl *, IvarFacts>
        IvarFactsMap;
    typedef std::map<const std::string, SharedObserverFacts>
        SharedObserverFactsMap;

    IvarFactsMap _ivarFactsMap;
    SharedObserverFactsMap _sharedObserverFactsMap;
    StringSet _pseudoInitMethods;
    bool _hasDealloc;

    ObjCImplFacts() : _hasDealloc(false) {}

    void ivarMayStoreSelfInUnsafeProperty(const clang::ObjCIvarDecl *ivarDecl,
                                          const std::string &propName);

    void ivarMayTargetSelf(const clang::ObjCIvarDecl *ivarDecl);

    void ivarMayObserveSelf(const clang::ObjCIvarDecl *ivarDecl);

    void sharedObjectMayObserveSelfInMethod(const std::string objectName,
                                            const std::string &methodName);
  };

 private:
  // current method being visited
  const clang::ObjCMethodDecl *_methDecl;

  // pointer to the structure recording facts over the current class
  // implementation
  struct ObjCImplFacts &_facts;

 public:
  FactFinder(const clang::ObjCMethodDecl *methDecl, struct ObjCImplFacts &facts)
      : _methDecl(methDecl), _facts(facts) {}

  // some pre-processing on the method declaration
  void VisitMethodDecl();

  // the useful visiting method
  void VisitObjCMessageExpr(const clang::ObjCMessageExpr *expr);

  // usual visiting logics
  void VisitChildren(const clang::Stmt *stmt);
  inline void VisitStmt(const clang::Stmt *stmt) { VisitChildren(stmt); }
};

} // end of namespace
