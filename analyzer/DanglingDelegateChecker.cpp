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
 * Highly complex and heuristic two-pass analyzer to detect memory unsafe
patterns in Objective C, so far related to delegate properties.
 *
 * Here is an example (see non regression tests for more).
<code>

@class Foo;

// external class
@interface Bar
@property (assign) Foo *delegate;
@end

// class being analyzed
@interface Foo : NSObject
@property (retain) Bar *bar;
@end

@implementation Foo

-(instancetype) init {
 if(self = [super init]) {
   // ...
   // "Registering" self in an unmanaged (non-zeroing)
   // pointer of an external object.
   self.bar.delegate = self;
 }
 return self;
}

// Here, the ARC-generated dealloc will not clear self.bar.delegate : this
// is unsafe because Bar could outlive Foo.

@end

</code>
 */

#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>

#include <iostream>

#include "PluginMainRegistry.h"
#include "DanglingDelegateCommon.h"
#include "DanglingDelegateFactFinder.h"

using namespace clang;
using namespace ento;

// TODO: addTarget, addObserver x2
// TODO: addObserverForName
// TODO: fb_addUpdateObserver
// TODO: use code-annotations instead of a hardcoded list of pseudo-init
// methods??
// TODO: try to use clang symbols to track references to (pseudo)ivars and
// support aliasing, use the 'assign' callback instead of catching BinOp stmts,
// use the deadsymbols callback

namespace DanglingDelegate {

typedef struct FactFinder::ObjCImplFacts ObjCImplFacts;
typedef struct FactFinder::IvarFacts IvarFacts;

// record the state of an "interesting" ivar during the analysis
struct IvarDynamicState {
  StringSet _assignPropertyWasCleared; // e.g. 'delegate' would be in the set if
                                       // the ivar _x has processed [_x
                                       // setDelegate:nil] in the code

  bool _targetWasCleared; // same with [_x removeTarget:self ..]

  bool _observerWasCleared; // same with [_x removeObserver:self ..]

  // Make a state where none of the expected events have happened yet
  IvarDynamicState() : _targetWasCleared(false), _observerWasCleared(false) {}

  IvarDynamicState(const IvarDynamicState &rhs)
      : _assignPropertyWasCleared(rhs._assignPropertyWasCleared),
        _targetWasCleared(rhs._targetWasCleared),
        _observerWasCleared(rhs._observerWasCleared) {}

  IvarDynamicState(const IvarDynamicState *rhs)
      : _assignPropertyWasCleared(rhs ? rhs->_assignPropertyWasCleared
                                      : StringSet()),
        _targetWasCleared(rhs ? rhs->_targetWasCleared : false),
        _observerWasCleared(rhs ? rhs->_observerWasCleared : false) {}

  // Make a state where all the expected events for this ivar already happened
  IvarDynamicState(const IvarFacts &ivarFacts)
      : _assignPropertyWasCleared(ivarFacts._mayStoreSelfInUnsafeProperty),
        _targetWasCleared(ivarFacts._mayTargetSelf),
        _observerWasCleared(ivarFacts._mayObserveSelf) {}

  bool operator==(const IvarDynamicState &rhs) const {
    // TODO
    return false;
  }

  void Profile(const llvm::FoldingSetNodeID &ID) const {
    // TODO
    // ID.AddInteger(...);
  }
};

} // end of namespace

REGISTER_MAP_WITH_PROGRAMSTATE(IvarMap,
                               const ObjCIvarDecl *,
                               DanglingDelegate::IvarDynamicState)

// hack to allow setting the initial state to a non default value at the
// beginning of trace
// (technically before the first statement)
REGISTER_TRAIT_WITH_PROGRAMSTATE(InitStateWasSet, bool)

// hack to remember the current ObjCInterfaceDecl in the state
REGISTER_TRAIT_WITH_PROGRAMSTATE(CurrentObjCInterfaceDecl, const void *)

namespace DanglingDelegate {

class DanglingDelegateChecker : public Checker<check::ASTDecl<ObjCImplDecl>,
                                               check::EndOfTranslationUnit,
                                               check::EndFunction,
                                               check::PreStmt<Stmt>,
                                               check::PostObjCMessage,
                                               eval::Assume> {
  std::unique_ptr<BugType> _bugType;
  mutable std::map<std::string, ObjCImplFacts> _implFacts;

  // tries to retrieve the syntactic facts of the current class interface
  // returns an unsafe reference to the value stored in the map
  const ObjCImplFacts *getCurrentFacts(
      const ObjCInterfaceDecl *topInterface) const;

  // verify that the given ivar (occuring in expr) is ok to be cleared
  void verifyIvarDynamicStateAgainstStaticFacts(const Expr &expr,
                                                const ObjCIvarDecl *ivarDecl,
                                                CheckerContext &context) const;

  // pseudo-init methods have a different state where all ivars are considered
  // cleared
  void resetInitialStateIfNeeded(CheckerContext &context) const;

 public:
  DanglingDelegateChecker();

  // launch the preliminary syntactic analysis
  void checkASTDecl(const ObjCImplDecl *implDecl,
                    AnalysisManager &manager,
                    BugReporter &bugReporter) const;

  // clear the syntactic facts at the end
  void checkEndOfTranslationUnit(const TranslationUnitDecl *tuDecl,
                                 AnalysisManager &manager,
                                 BugReporter &bugReporter) const;

  void checkEndFunction(CheckerContext &context) const;

  void checkPreStmt(const Stmt *stmt, CheckerContext &context) const;

  void checkPostObjCMessage(const ObjCMethodCall &message,
                            CheckerContext &context) const;

  ProgramStateRef evalAssume(ProgramStateRef state,
                             const SVal &cond,
                             bool assumption) const;
};

DanglingDelegateChecker::DanglingDelegateChecker() {
  _bugType.reset(new BugType(
      this, "Leaking unsafe reference to self", "Memory error (Facebook)"));
  _bugType->setSuppressOnSink(true);
}

void DanglingDelegateChecker::checkEndOfTranslationUnit(
    const TranslationUnitDecl *tuDecl,
    AnalysisManager &manager,
    BugReporter &bugReporter) const {
  _implFacts.clear();
}

static void verifyAndReportDangerousProperties(
    const StringSet &dangerousProperties,
    const StringSet &clearedProperties,
    const std::string &ivarName,
    const QualType ivarType,
    const std::string &declName,
    const std::function<void(StringRef)> &emitBugReport) {
  // Verify that all the dangerous properties have been cleared
  for (auto it = dangerousProperties.begin(), end = dangerousProperties.end();
       it != end;
       it++) {

    if (clearedProperties.find(*it) == clearedProperties.end()) {
      llvm::SmallString<128> buf;
      llvm::raw_svector_ostream os(buf);
      std::string className;
      const ObjCObjectPointerType *classType =
          dyn_cast_or_null<ObjCObjectPointerType>(ivarType.getTypePtr());
      if (classType && classType->getInterfaceDecl()) {
        className = classType->getInterfaceDecl()->getName();
      }

      os << "Leaking unsafe reference to self stored in " << ivarName << "."
         << *it;
      if (declName != "") {
        os << " (in " << declName << ")";
      }
      os << ". ";

      os << "The assign property '" << *it << "' of the ";
      if (className != "") {
        os << "instance of " << className;
      } else {
        os << "object";
      }
      os << " stored in '" << ivarName
         << "' appears to occasionally point to self. ";

      os << "For memory safety, you need to clear this property explicitly "
            "before losing reference to this object, typically by adding a "
            "line: '"
         << ivarName << "." << *it << " = nil;'. ";

      os << "In case of a false warning, consider adding an assert instead: "
            "'FBAssert("
         << ivarName << "." << *it
         << " != self);' or, if applicable: 'FBAssert(!" << ivarName << ");'.";

      emitBugReport(os.str());
    }
  }
}

void DanglingDelegateChecker::checkASTDecl(const ObjCImplDecl *implDecl,
                                           AnalysisManager &manager,
                                           BugReporter &bugReporter) const {

  const NamedDecl *namedDecl = dyn_cast<NamedDecl>(implDecl);
  if (!namedDecl) {
    assert(false);
    return;
  }
  std::string name = namedDecl->getNameAsString();

  // obtain the map of facts for the current class implementation, possibly a
  // new one
  ObjCImplFacts &facts = _implFacts[name];

  for (ObjCImplDecl::method_iterator it = implDecl->meth_begin(),
                                     end = implDecl->meth_end();
       it != end;
       ++it) {
    FactFinder factFinder(*it, facts);

    factFinder.VisitMethodDecl();

    Stmt *body = (*it)->getBody();
    if (body) {
      factFinder.Visit(body);
    }
  }

  PathDiagnosticLocation pLoc(implDecl->getLocation(),
                              bugReporter.getSourceManager());
  const std::function<void(StringRef)> emitBug(
      [implDecl, &pLoc, &bugReporter, this](StringRef str) {
        bugReporter.EmitBasicReport(implDecl,
                                    this,
                                    "Leaking unsafe reference to self",
                                    "Memory error (Facebook)",
                                    str,
                                    pLoc);
      });

  // The default dealloc in ARC mode does not clear delegates.
  if (!facts._hasDealloc && manager.getLangOpts().ObjCAutoRefCount) {

    const StringSet emptySet;
    for (auto it = facts._ivarFactsMap.begin(), end = facts._ivarFactsMap.end();
         it != end;
         it++) {
      verifyAndReportDangerousProperties(
          it->second._mayStoreSelfInUnsafeProperty,
          emptySet,
          it->first->getNameAsString(),
          it->first->getType(),
          "ARC-generated dealloc",
          emitBug);
    }
  }
}

void DanglingDelegateChecker::checkEndFunction(CheckerContext &context) const {
  // dealloc implicitly releases ivars only in ARC-mode
  if (!context.getLangOpts().ObjCAutoRefCount) {
    return;
  }

  const ObjCImplFacts *facts =
      getCurrentFacts(getCurrentTopClassInterface(context));
  if (!facts) {
    return;
  }
  const ObjCMethodDecl *decl =
      dyn_cast_or_null<ObjCMethodDecl>(context.getStackFrame()->getDecl());
  if (!decl || decl->getSelector().getAsString() != "dealloc") {
    return;
  }

  const std::function<void(StringRef)> emitBug([this, decl, &context](
      StringRef str) {
    auto report =
        llvm::make_unique<BugReport>(*_bugType, str, context.getPredecessor());
    report->addRange(decl->getSourceRange());
    context.emitReport(std::move(report));
  });
  ProgramStateRef state = context.getState();

  // Verify that all the dangerous properties have been cleared
  const IvarDynamicState emptyIds;
  for (auto it = facts->_ivarFactsMap.begin(), end = facts->_ivarFactsMap.end();
       it != end;
       it++) {

    const ObjCIvarDecl *ivarDecl = it->first;
    const StringSet &dangerousProperties =
        it->second._mayStoreSelfInUnsafeProperty;
    const IvarDynamicState *ids = state->get<IvarMap>(ivarDecl);
    if (!ids) {
      ids = &emptyIds;
    }
    const StringSet &clearedProperties = ids->_assignPropertyWasCleared;

    verifyAndReportDangerousProperties(dangerousProperties,
                                       clearedProperties,
                                       ivarDecl->getNameAsString(),
                                       ivarDecl->getType(),
                                       "ARC-generated code",
                                       emitBug);
  }
}

const ObjCImplFacts *DanglingDelegateChecker::getCurrentFacts(
    const ObjCInterfaceDecl *topInterface) const {
  if (!topInterface) {
    return NULL;
  }
  const NamedDecl *namedDecl = dyn_cast<NamedDecl>(topInterface);
  if (!namedDecl) {
    return NULL;
  }
  std::string intName = namedDecl->getNameAsString();
  return &_implFacts.at(intName);
}

void DanglingDelegateChecker::verifyIvarDynamicStateAgainstStaticFacts(
    const Expr &expr,
    const ObjCIvarDecl *ivarDecl,
    CheckerContext &context) const {

  // first retrieve the dangerous 'static' facts we know about the ivar
  if (!ivarDecl) {
    return;
  }
  const ObjCImplFacts *facts =
      getCurrentFacts(getCurrentTopClassInterface(context));
  if (!facts ||
      facts->_ivarFactsMap.find(ivarDecl) == facts->_ivarFactsMap.end()) {
    // not an interesting ivar (no entry)
    return;
  }
  const IvarFacts &ivarFacts = facts->_ivarFactsMap.at(ivarDecl);
  std::string ivarName = ivarDecl->getNameAsString();

  // second retrieve the current 'dynamic' state of the ivar
  ProgramStateRef state = context.getState();
  const IvarDynamicState emptyIds;
  const IvarDynamicState *ids = state->get<IvarMap>(ivarDecl);
  if (!ids) {
    ids = &emptyIds;
  }

  // Verify that all the dangerous properties have been cleared
  const StringSet &dangerousProperties =
      ivarFacts._mayStoreSelfInUnsafeProperty;
  const StringSet &clearedProperties = ids->_assignPropertyWasCleared;
  const std::function<void(StringRef)> emitBug([this, &context, &expr](
      StringRef str) {
    auto report =
        llvm::make_unique<BugReport>(*_bugType, str, context.getPredecessor());
    report->addRange(expr.getSourceRange());
    context.emitReport(std::move(report));
  });

  verifyAndReportDangerousProperties(dangerousProperties,
                                     clearedProperties,
                                     ivarName,
                                     ivarDecl->getType(),
                                     "",
                                     emitBug);

  // Verify that the object in the ivar is not 'observing' self (TODO)
  //    if (ivarFacts._mayObserveSelf && !ids->_observerWasCleared) {
  //    }

  // Verify that the object in the ivar is not 'targeting' self (TODO)
  //    if (ivarFacts._mayTargetSelf && !ids->_targetWasCleared) {
  //    }
}

void DanglingDelegateChecker::resetInitialStateIfNeeded(
    CheckerContext &context) const {
  // nothing to do if we did it already
  ProgramStateRef state = context.getState();
  if (state->get<InitStateWasSet>()) {
    return;
  }
  // record the InterfaceDecl if any
  const ObjCInterfaceDecl *topInterface = getCurrentTopClassInterface(context);
  if (topInterface) {
    state = state->set<CurrentObjCInterfaceDecl>((void *)topInterface);
    // see if we are in a 'pseudo-init' method
    const ObjCMethodDecl *decl = dyn_cast_or_null<ObjCMethodDecl>(
        context.getCurrentAnalysisDeclContext()->getDecl());
    if (decl) {
      const ObjCImplFacts *facts = getCurrentFacts(topInterface);
      if (facts &&
          facts->_pseudoInitMethods.find(decl->getNameAsString()) !=
              facts->_pseudoInitMethods.end()) {
        // override the maps to assume that everything was cleared already
        const std::map<const ObjCIvarDecl *, IvarFacts> &ivarFactsMap =
            facts->_ivarFactsMap;
        for (auto it = ivarFactsMap.begin(), end = ivarFactsMap.end();
             it != end;
             ++it) {
          state = state->set<IvarMap>(it->first, IvarDynamicState(it->second));
        }
      }
    }
  }
  // in any case mark the state as initialized
  state = state->set<InitStateWasSet>(true);
  context.addTransition(state);
}

void DanglingDelegateChecker::checkPreStmt(const Stmt *stmt,
                                           CheckerContext &context) const {
  // hack to deal with pseudo-init methods
  resetInitialStateIfNeeded(context);

  // Next we track assignments to "interesting" ivars.
  // These are not objc messages so we need to deal with them separately.

  // no need for checking ivar assignments in non-ARC mode (the verification is
  // on the release)
  if (!context.getLangOpts().ObjCAutoRefCount) {
    return;
  }

  const BinaryOperator *binOp = dyn_cast<BinaryOperator>(stmt);
  if (!binOp || !binOp->isAssignmentOp()) {
    return;
  }

  // look for an ivarref on the left of the assignment
  const Expr *lhs = binOp->getLHS()->IgnoreParenCasts();
  if (!lhs || !lhs->getType()->getAsObjCInterfacePointerType()) {
    return;
  }
  const ObjCIvarRefExpr *ivarRef = dyn_cast<ObjCIvarRefExpr>(lhs);
  if (!ivarRef) {
    return;
  }
  const ObjCIvarDecl *ivarDecl = ivarRef->getDecl();
  if (!ivarDecl) {
    return;
  }

  // want a non-null previous value in the ivar
  SVal ivarLVal = context.getSVal(lhs);
  const MemRegion *region = ivarLVal.getAsRegion();
  if (region) {
    SVal ivarRVal = context.getState()->getSVal(region);
    if (isKnownToBeNil(ivarRVal, context)) {
      // we are sure that the ivar is nil => abort
      return;
    }
  }
  verifyIvarDynamicStateAgainstStaticFacts(*binOp, ivarDecl, context);
}

void DanglingDelegateChecker::checkPostObjCMessage(
    const ObjCMethodCall &message, CheckerContext &context) const {
  // if the call was inlined, there is nothing else to do
  if (context.wasInlined) {
    return;
  }

  const ObjCMessageExpr *expr = message.getOriginExpr();
  if (!expr) {
    assert(false);
    return;
  }

  // want an instance message to a non-null receiver
  const Expr *receiver = expr->getInstanceReceiver();
  if (!receiver) {
    return;
  }
  if (isKnownToBeNil(message.getReceiverSVal(), context)) {
    // we are sure that the receiver is nil => abort mission
    return;
  }

  // retrieves the static facts on ivars
  const ObjCImplFacts *facts =
      getCurrentFacts(getCurrentTopClassInterface(context));
  if (!facts) {
    return;
  }

  // First we try to detect the setting of an interesting property of self
  if (isObjCSelfExpr(receiver)) {
    const ObjCPropertyDecl *propDecl =
        matchObjCMessageWithPropertySetter(*expr);
    if (propDecl) {
      // To mitigate false positives, we verify only setters that have an
      // unknown body.
      // (Setters with a known body are unfortunately not always inlined.)
      RuntimeDefinition runtimeDefinition = message.getRuntimeDefinition();
      if (!runtimeDefinition.getDecl() ||
          runtimeDefinition.getDecl()->isImplicit()) {
        verifyIvarDynamicStateAgainstStaticFacts(
            *expr, propDecl->getPropertyIvarDecl(), context);
      }

      // Next we deal with a possible assignment self.x = nil to prevent further
      // warning
      const ObjCIvarDecl *ivarDecl = propDecl->getPropertyIvarDecl();
      if (ivarDecl &&
          facts->_ivarFactsMap.find(ivarDecl) != facts->_ivarFactsMap.end()) {
        SVal value = message.getArgSVal(0);
        if (isKnownToBeNil(value, context)) {
          // mark the corresponding ivar as cleared
          ProgramStateRef state = context.getState();
          IvarDynamicState clearedStateForIvar(
              facts->_ivarFactsMap.at(ivarDecl));
          state = state->set<IvarMap>(ivarDecl, clearedStateForIvar);
          context.addTransition(state);
        }
      }

      return;
    }
  }

  // What follows detects when we correctly clear the references inside an ivar
  // This is dual to FactFinder::VisitObjCMessageExpr

  std::string selectorStr = expr->getSelector().getAsString();
  // do we have a first argument equal to self?
  bool paramIsSelf = isObjCSelfExpr(getArgOfObjCMessageExpr(*expr, 0));

  // is the receiver an interesting ivar?
  const ObjCIvarDecl *ivarDecl = matchIvarLValueExpression(*receiver);
  if (ivarDecl &&
      facts->_ivarFactsMap.find(ivarDecl) != facts->_ivarFactsMap.end()) {

    // is this a release?
    if (selectorStr == "release" || selectorStr == "autorelease") {
      assert(!paramIsSelf);
      verifyIvarDynamicStateAgainstStaticFacts(*expr, ivarDecl, context);
      return;
    }

    // Prepare a new state to modify, associated with the receiver
    ProgramStateRef state = context.getState();
    // Copy the previous state if present
    IvarDynamicState ivarState(state->get<IvarMap>(ivarDecl));

    // is this a setter of an assign property?
    const ObjCPropertyDecl *propDecl =
        matchObjCMessageWithPropertySetter(*expr);
    if (propDecl) {
      if (propDecl->getSetterKind() != ObjCPropertyDecl::Assign) {
        return;
      }
      std::string propName = propDecl->getNameAsString();
      if (!paramIsSelf) {
        // the property is now considered cleared
        ivarState._assignPropertyWasCleared.insert(propName);
      } else {
        // "unclear" the property since we just stored self again in it
        ivarState._assignPropertyWasCleared.erase(propName);
      }

    } else if (paramIsSelf && StringRef(selectorStr).startswith("removeTarget:")) {
      ivarState._targetWasCleared = true;
    } else if (paramIsSelf && StringRef(selectorStr).startswith("removeObserver:")) {
      ivarState._observerWasCleared = true;
    } else {
      // return to avoid transitioning to a new identical state
      return;
    }

    // write the new state
    state = state->set<IvarMap>(ivarDecl, ivarState);
    context.addTransition(state);
    return;
  }

  // TODO: is the receiver an interesting "observable singleton object"?
  //    string receiverObjectName = matchObservableSingletonObject(receiver);
  //    if (!receiverObjectName.empty()) {
  //
  //      if (paramIsSelf && selectorStr.startswith("addObserver:")) {
  //        // TODO
  //      }
  //
  //    }
}

// Tests the opcode
static bool matchOpcode(BinaryOperator::Opcode op,
                        bool assumption,
                        bool wantEquality) {
  switch (op) {
  case BO_EQ:
    return (assumption == wantEquality);
  case BO_NE:
    return (assumption != wantEquality);
  default:
    return false;
  }
}

template <class A, class B>
static bool matchConditionWithComparison(const SVal &cond,
                                         bool assumption,
                                         bool wantEquality,
                                         const A **foundA,
                                         const B **foundB) {
  // We are interested in SymSymExpr's
  const SymSymExpr *sse =
      dyn_cast_or_null<SymSymExpr>(cond.getAsSymbolicExpression());
  if (!sse || !matchOpcode(sse->getOpcode(), assumption, wantEquality)) {
    // abort
    return false;
  }

  // Tries to matches the instances of A and B in the lhs and rhs symbolic
  // expressions
  const SymExpr *lhs = sse->getLHS();
  const SymExpr *rhs = sse->getRHS();

  const A *symA = dyn_cast_or_null<A>(lhs);
  const B *symB = dyn_cast_or_null<B>(rhs);
  if (!symA && !symB) {
    // no luck yet, let's switch args
    symA = dyn_cast_or_null<A>(rhs);
    symB = dyn_cast_or_null<B>(lhs);
  }

  if (symA && symB) {
    *foundA = symA;
    *foundB = symB;
    return true;
  }

  return false;
}

template <class A>
static bool matchConditionWithIntComparison(const SVal &cond,
                                            bool assumption,
                                            bool wantEquality,
                                            const A **foundA,
                                            const llvm::APSInt **foundInt) {
  const A *expr = NULL;
  const llvm::APSInt *val = NULL;
  BinaryOperator::Opcode opcode;

  const SymIntExpr *sie =
      dyn_cast_or_null<SymIntExpr>(cond.getAsSymbolicExpression());
  if (sie) {
    // Tries to matches the instances of A and B in the lhs and rhs symbolic
    // expressions
    expr = dyn_cast_or_null<A>(sie->getLHS());
    val = &sie->getRHS();
    opcode = sie->getOpcode();
  } else {
    const IntSymExpr *ise =
        dyn_cast_or_null<IntSymExpr>(cond.getAsSymbolicExpression());
    if (ise) {
      // Tries to matches the instances of A and B in the lhs and rhs symbolic
      // expressions
      expr = dyn_cast_or_null<A>(ise->getRHS());
      val = &ise->getLHS();
      opcode = ise->getOpcode();
    }
  }

  if (expr && val && matchOpcode(opcode, assumption, wantEquality)) {
    *foundA = expr;
    *foundInt = val;
    return true;
  }

  return false;
}

// match expr against [_x delegate] or [self.x delegate]
static bool matchObjCMessageExprForInterestingDelegate(
    const ObjCMessageExpr &expr,
    const ObjCImplFacts &facts,
    const ObjCIvarDecl **foundIvar,
    const ObjCPropertyDecl **foundProperty) {
  // want an instance message to a non-null receiver
  const Expr *receiver = expr.getInstanceReceiver();
  if (!receiver) {
    return false;
  }
  const ObjCIvarDecl *ivarDecl = matchIvarLValueExpression(*receiver);
  if (!ivarDecl) {
    return false;
  }
  if (facts._ivarFactsMap.find(ivarDecl) == facts._ivarFactsMap.end()) {
    // not an interesting ivar (no entry)
    return false;
  }

  // is this a getter to an assign property?
  const ObjCPropertyDecl *propDecl = matchObjCMessageWithPropertyGetter(expr);
  if (propDecl) {
    if (propDecl->getSetterKind() != ObjCPropertyDecl::Assign) {
      return false;
    }
    // here we go!
    *foundIvar = ivarDecl;
    *foundProperty = propDecl;
    return true;
  }
  return false;
}

// Matches the internal representation of tests of this form (or equivalent):
//   _x.delegate != self
//   self.x.delegate != self
// for an unsafe delegate property of ivar _x.
//
// In practice, the internal representations look like this: (as displayed by
// cond.dump())
//   (conj_$5{Test *}) != (reg_$0<self>)
static bool matchConditionWithInterestingDelegateNotEqualToSelf(
    const SVal &cond,
    bool assumption,
    const ObjCImplFacts &facts,
    const ObjCIvarDecl **foundIvar,
    const ObjCPropertyDecl **foundProperty) {
  const SymbolConjured *symConj;
  const SymbolRegionValue *symRVal;
  if (!matchConditionWithComparison<SymbolConjured, SymbolRegionValue>(
          cond, assumption, false, &symConj, &symRVal)) {
    // abort
    return false;
  }
  assert(symConj && symRVal);

  // We are looking for an assertion that ObjCMessageExprReadingAnIvarProperty
  // != self
  // first look for  'self'
  const DeclRegion *declRegion =
      dyn_cast_or_null<DeclRegion>(symRVal->getRegion());
  if (!declRegion) {
    return false;
  }
  const ImplicitParamDecl *decl =
      dyn_cast_or_null<ImplicitParamDecl>(declRegion->getDecl());
  // FIXME: do not use the name?
  if (!decl || decl->getNameAsString() != "self") {
    return false;
  }

  const ObjCMessageExpr *expr =
      dyn_cast_or_null<ObjCMessageExpr>(symConj->getStmt());
  return (expr ? matchObjCMessageExprForInterestingDelegate(
                     *expr, facts, foundIvar, foundProperty)
               : false);
}

// Matches the internal representation of tests of this form (or equivalent):
//   _x.delegate == nil
//   self.x.delegate == nil
// for an unsafe delegate property of ivar _x.
//
// In practice, the internal representations look like this:  (as displayed by
// cond.dump())
//   (conj_$5{Test *}) == 0U
static bool matchConditionWithInterestingDelegateEqualToNil(
    const SVal &cond,
    bool assumption,
    const ObjCImplFacts &facts,
    const ObjCIvarDecl **foundIvar,
    const ObjCPropertyDecl **foundProperty) {
  const SymbolConjured *symConj = NULL;
  const llvm::APSInt *val = NULL;
  if (!matchConditionWithIntComparison<SymbolConjured>(
          cond, assumption, true, &symConj, &val)) {
    return false;
  }
  assert(symConj && val);
  if (*val != 0) {
    return false;
  }

  const ObjCMessageExpr *expr =
      dyn_cast_or_null<ObjCMessageExpr>(symConj->getStmt());
  return (expr ? matchObjCMessageExprForInterestingDelegate(
                     *expr, facts, foundIvar, foundProperty)
               : false);
}

// Matches the internal representation of tests of this form (or equivalent):
//   self.x == nil
// for an ivar _x having unsafe properties.
//
// In practice, the internal representations look like this:  (as displayed by
// cond.dump())
//   (conj_$4{Worker *}) == 0U
static bool matchConditionWithInterestingPropertyEqualToNil(
    const SVal &cond,
    bool assumption,
    const ObjCImplFacts &facts,
    const ObjCIvarDecl **foundIvar) {

  const SymbolConjured *symConj = NULL;
  const llvm::APSInt *val = NULL;
  if (!matchConditionWithIntComparison<SymbolConjured>(
          cond, assumption, true, &symConj, &val)) {
    return false;
  }
  assert(symConj && val);
  if (*val != 0) {
    return false;
  }

  const Expr *expr = dyn_cast_or_null<Expr>(symConj->getStmt());
  if (!expr) {
    return false;
  }

  const ObjCIvarDecl *ivarDecl = matchIvarLValueExpression(*expr);
  if (!ivarDecl) {
    const ObjCMessageExpr *msgExpr = dyn_cast_or_null<ObjCMessageExpr>(expr);
    if (!msgExpr) {
      return false;
    }
    const ObjCPropertyDecl *propDecl =
        matchObjCMessageWithPropertyGetter(*msgExpr);
    if (!propDecl) {
      return false;
    }
    ivarDecl = propDecl->getPropertyIvarDecl();
  }

  if (!ivarDecl ||
      facts._ivarFactsMap.find(ivarDecl) == facts._ivarFactsMap.end()) {
    return false;
  }

  *foundIvar = ivarDecl;
  return true;
}

// Matches the internal representation of tests of this form (or equivalent):
//   _x == nil
// for an ivar _x having unsafe properties.
//
// In practice, the internal representations look like this:  (as displayed by
// cond.dump())
//   (reg_$1<ivar{SymRegion{reg_$0<self>},_x}>) == 0U
// or in some cases:
//   (derived_$6{conj_$2{int},ivar{SymRegion{reg_$0<self>},_worker2}}) == 0U
static bool matchConditionWithInterestingIvarEqualToNil(
    const SVal &cond,
    bool assumption,
    const ObjCImplFacts &facts,
    const ObjCIvarDecl **foundIvar) {
  const ObjCIvarRegion *ivarReg = NULL;
  const llvm::APSInt *val = NULL;

  const SymbolRegionValue *sym = NULL;
  if (matchConditionWithIntComparison<SymbolRegionValue>(
          cond, assumption, true, &sym, &val)) {
    assert(sym && val);
    ivarReg = dyn_cast_or_null<ObjCIvarRegion>(sym->getRegion());
  } else {
    const SymbolDerived *symD = NULL;
    if (matchConditionWithIntComparison<SymbolDerived>(
            cond, assumption, true, &symD, &val)) {
      assert(symD && val);
      ivarReg = dyn_cast_or_null<ObjCIvarRegion>(symD->getRegion());
    }
  }

  if (!ivarReg || !val || *val != 0) {
    return false;
  }
  const ObjCIvarDecl *ivarDecl = ivarReg->getDecl();
  if (!ivarDecl ||
      facts._ivarFactsMap.find(ivarDecl) == facts._ivarFactsMap.end()) {
    return false;
  }
  *foundIvar = ivarDecl;
  return true;
}

// Here we want to intercept tests that restrict the current branch to
// "favorable cases"
// We then mark the state accordingly in a way that will survive to whatever
// happens next in the trace.
// This is unsound but for this trace at least the programmer seems to know what
// he/she is doing.
ProgramStateRef DanglingDelegateChecker::evalAssume(ProgramStateRef state,
                                                    const SVal &cond,
                                                    bool assumption) const {
  const ObjCInterfaceDecl *currentInterface =
      (const ObjCInterfaceDecl *)state->get<CurrentObjCInterfaceDecl>();
  const ObjCImplFacts *facts = getCurrentFacts(currentInterface);
  if (!facts) {
    // we don't know where we are => abort
    return state;
  }

  const ObjCIvarDecl *ivarDecl;
  const ObjCPropertyDecl *propDecl;
  if (matchConditionWithInterestingDelegateNotEqualToSelf(
          cond, assumption, *facts, &ivarDecl, &propDecl) ||
      matchConditionWithInterestingDelegateEqualToNil(
          cond, assumption, *facts, &ivarDecl, &propDecl)) {
    assert(ivarDecl && propDecl);
    IvarDynamicState ivarState(state->get<IvarMap>(ivarDecl));

    // Mark the property as cleared
    std::string propName = propDecl->getNameAsString();
    ivarState._assignPropertyWasCleared.insert(propName);

    return state->set<IvarMap>(ivarDecl, ivarState);
  }

  if (matchConditionWithInterestingPropertyEqualToNil(
          cond, assumption, *facts, &ivarDecl) ||
      matchConditionWithInterestingIvarEqualToNil(
          cond, assumption, *facts, &ivarDecl)) {
    assert(ivarDecl);
    IvarDynamicState clearedStateForIvar(facts->_ivarFactsMap.at(ivarDecl));
    return state->set<IvarMap>(ivarDecl, clearedStateForIvar);
  }

  return state;
}

} // end of namespace

// needed because REGISTER_CHECKER_IN_PLUGIN computes function identifiers with
// the given name
using namespace DanglingDelegate;

REGISTER_CHECKER_IN_PLUGIN(
    DanglingDelegateChecker,
    "facebook.DanglingDelegateChecker",
    "Find unsafe references that should be nil-ed before releasing an object")
