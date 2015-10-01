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
 * Utility functions for the "dangling delegate" checker.
 */

#pragma once

#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>
#include <clang/AST/StmtVisitor.h>

namespace DanglingDelegate {

typedef std::set<std::string> StringSet;

// ex: "setDelegate" -> "delegate"
const std::string getPropertyNameFromSetterSelector(
    const clang::Selector &selector);

// more robust version using ignoreOpaqueValParenImpCasts
bool isObjCSelfExpr(const clang::Expr *expr);

// returns NULL instead of failing if param does not exist
const clang::Expr *getArgOfObjCMessageExpr(const clang::ObjCMessageExpr &expr,
                                           unsigned index);

// finds the name of the current class being analyzed (if any)
const clang::ObjCInterfaceDecl *getCurrentTopClassInterface(
    const clang::ento::CheckerContext &ctx);

// strips expressions a bit more than IgnoreParenImpCasts
const clang::Expr *ignoreOpaqueValParenImpCasts(const clang::Expr *expr);

// ex: [obj x]
const clang::ObjCPropertyDecl *matchObjCMessageWithPropertyGetter(
    const clang::ObjCMessageExpr &expr);

// ex: [obj setX:0];
const clang::ObjCPropertyDecl *matchObjCMessageWithPropertySetter(
    const clang::ObjCMessageExpr &expr);

// matches expressions _x and self.x
const clang::ObjCIvarDecl *matchIvarLValueExpression(const clang::Expr &expr);

// matches interesting singleton objects [NSNotificationCenter defaultCenter]
// and makes an identifying string out of it
const std::string matchObservableSingletonObject(const clang::Expr &expr);

// true when the SVal is known to be nil (does not add constraints on the path,
// use this only for error suppression)
bool isKnownToBeNil(const clang::ento::SVal &sVal,
                    clang::ento::CheckerContext &context);

} // end of namespace
