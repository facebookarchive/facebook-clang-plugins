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
 * This checker looks for
 * - suspicious duplicated properties (e.g. trying to redeclare a property to
 * make it readwrite),
 * - suspicious @synthesize (e.g. not in the right class implementation), and
 * - unnecessary synthesize statements.
 */

#include "PluginMainRegistry.h"

#include <clang/StaticAnalyzer/Core/BugReporter/BugReporter.h>
#include <clang/StaticAnalyzer/Core/BugReporter/BugType.h>
#include <clang/StaticAnalyzer/Core/Checker.h>
#include <clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h>
#include <clang/StaticAnalyzer/Core/CheckerRegistry.h>
#include <clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h>
#include <clang/AST/DeclObjC.h>
#include "PluginMainRegistry.h"
#include <string>

using namespace clang;
using namespace ento;
using std::string;

class PropertySynthesizeChecker
    : public Checker<check::ASTDecl<ObjCImplDecl>,
                     check::ASTDecl<ObjCPropertyImplDecl>> {

 private:
  static bool isInheritedFromProtocol(const ObjCPropertyImplDecl *implDecl,
                                      const ObjCInterfaceDecl *interfaceDecl) {
    typedef ObjCProtocolDecl::PropertyMap PropertyMap;
    typedef ObjCProtocolDecl::PropertyDeclOrder PropertyDeclOrder;
    typedef llvm::SmallPtrSet<ObjCProtocolDecl *, 8> ObjCProtocolDeclSet;

    const ObjCPropertyDecl *propertyDecl = implDecl->getPropertyDecl();
    assert(propertyDecl);

    ObjCProtocolDeclSet Protocols;
    implDecl->getASTContext().CollectInheritedProtocols(interfaceDecl,
                                                        Protocols);

    for (ObjCProtocolDeclSet::iterator PI = Protocols.begin(),
                                       PE = Protocols.end();
         PI != PE;
         ++PI) {
      PropertyMap PM;
      PropertyDeclOrder PO;
      (*PI)->collectPropertiesToImplement(PM, PO);

      for (PropertyMap::iterator QI = PM.begin(), QE = PM.end(); QI != QE;
           ++QI) {
        if (QI->second == propertyDecl)
          return true;
      }
    }

    return false;
  }

  static bool findPropertyInParentClasses(
      std::string &propName,
      bool matchOnlyReadOnly,
      const ObjCInterfaceDecl *interfaceDecl,
      std::string *outClassName) {

    interfaceDecl = interfaceDecl->getSuperClass();

    while (interfaceDecl) {
      for (ObjCContainerDecl::prop_iterator P = interfaceDecl->prop_begin(),
                                            E = interfaceDecl->prop_end();
           P != E;
           ++P) {

        if ((*P)->getNameAsString() == propName &&
            (!matchOnlyReadOnly || (*P)->isReadOnly())) {
          if (outClassName) {
            *outClassName = interfaceDecl->getNameAsString();
          }
          return true;
        }
      }
      interfaceDecl = interfaceDecl->getSuperClass();
    }

    return false;
  }

  static bool hasUserDefinedMethod(const ObjCMethodDecl *D,
                                   const ObjCImplDecl *implDecl) {
    const ObjCMethodDecl *MD =
        implDecl->getMethod(D->getSelector(), D->isInstanceMethod());
    return MD != NULL && !MD->isImplicit() &&
           MD->isThisDeclarationADefinition();
  }

  static bool hasUserDefinedGetter(const ObjCPropertyDecl *propDecl,
                                   const ObjCImplDecl *implDecl) {
    ObjCMethodDecl *MD = propDecl->getGetterMethodDecl();
    return MD != NULL && hasUserDefinedMethod(MD, implDecl);
  }

  static bool hasUserDefinedSetter(const ObjCPropertyDecl *propDecl,
                                   const ObjCImplDecl *implDecl) {
    ObjCMethodDecl *MD = propDecl->getSetterMethodDecl();
    return MD != NULL && hasUserDefinedMethod(MD, implDecl);
  }

  void scanPropertyDecl(const ObjCPropertyDecl *propDecl,
                        const ObjCImplDecl *implDecl,
                        AnalysisManager &mgr,
                        BugReporter &BR) const {
    const DeclContext *declContext = propDecl->getDeclContext();
    assert(declContext);
    const ObjCInterfaceDecl *interfaceDecl =
        dyn_cast<ObjCInterfaceDecl>(declContext);
    assert(interfaceDecl);

    std::string propName = propDecl->getNameAsString();
    bool isReadWrite = 0 != (ObjCPropertyDecl::OBJC_PR_readwrite &
                             propDecl->getPropertyAttributes());
    bool hasLocalIvar = propDecl->getPropertyIvarDecl() != NULL;
    bool hasLocalSetter = hasUserDefinedSetter(propDecl, implDecl);

    if (!isReadWrite || hasLocalIvar || hasLocalSetter) {
      // we should be safe
      return;
    }

    std::string className;
    if (findPropertyInParentClasses(
            propName, true, interfaceDecl, &className)) {
      PathDiagnosticLocation L =
          PathDiagnosticLocation::create(propDecl, BR.getSourceManager());
      BR.EmitBasicReport(propDecl,
                         this,
                         "Duplicate properties",
                         "Semantic error (Facebook)",
                         "The property " + propName +
                             " has been declared read-only in parent class " +
                             className + ". " +
                             "The setter of the local read-write property will "
                             "not be synthesized. Consider using a @protected "
                             "ivar and/or an class extension in the original "
                             "parent class instead.",
                         L);
    }
  }

  void scanPropertyImplDecl(const ObjCPropertyImplDecl *propImplDecl,
                            const ObjCImplDecl *implDecl,
                            AnalysisManager &mgr,
                            BugReporter &BR) const {
    assert(propImplDecl);

    // nothing to say about dynamic properties
    if (propImplDecl->getPropertyImplementation() ==
        ObjCPropertyImplDecl::Dynamic) {
      return;
    }

    const ObjCPropertyDecl *propDecl = propImplDecl->getPropertyDecl();
    if (!propDecl) {
      return;
    }
    string propName = propDecl->getNameAsString();

    const ObjCInterfaceDecl *interfaceDecl = implDecl->getClassInterface();
    assert(interfaceDecl);

    std::string className;
    if (findPropertyInParentClasses(
            propName, false, interfaceDecl, &className)) {
      PathDiagnosticLocation L =
          PathDiagnosticLocation::create(propImplDecl, BR.getSourceManager());

      BR.EmitBasicReport(
          propImplDecl,
          this,
          "Duplicate synthesize statement",
          "Semantic error (Facebook)",
          "The property " + propName +
              " was originally declared in parent class " + className +
              ". "
              "The @synthesize statement here will create a new distinct ivar. "
              "This is usually not what you want. "
              "If this property was meant to be 'abstract' in the parent "
              "class, consider declaring it in a protocol "
              "or rewriting it as regular methods.",
          L);
      return;
    }

    ASTContext &ctx = propImplDecl->getASTContext();
    string defaultName = propDecl->getDefaultSynthIvarName(ctx)->getName();
    const ObjCIvarDecl *propIvarDecl = propImplDecl->getPropertyIvarDecl();
    string varName = propIvarDecl->getNameAsString();

    if (defaultName != varName ||
        isInheritedFromProtocol(propImplDecl, interfaceDecl)) {
      // The @synthesize seems useful
      return;
    }

    bool isReadOnly = 0 != (ObjCPropertyDecl::OBJC_PR_readonly &
                            propDecl->getPropertyAttributes());
    bool hasLocalSetterAndGetters =
        hasUserDefinedGetter(propDecl, implDecl) &&
        (isReadOnly || hasUserDefinedSetter(propDecl, implDecl));
    if (hasLocalSetterAndGetters ||
        interfaceDecl->isObjCRequiresPropertyDefs()) {
      // Auto-synthesis is deactivated when no setter/getter missing
      // or if one the super classes is tagged with
      // __attribute__((objc_requires_property_definitions))
      return;
    }
    const QualType propQualType = propDecl->getType();
    std::string propType = propQualType.getDesugaredType(ctx).getAsString();
    std::string ivarType = propIvarDecl->getUsageType(propQualType)
      .getDesugaredType(ctx).getAsString();
    if (propType != ivarType) {
      // The @synthesize seems useful when types between
      // the property and the ivar differ
      return;
    }
    PathDiagnosticLocation L =
        PathDiagnosticLocation::create(propImplDecl, BR.getSourceManager());
    BR.EmitBasicReport(propImplDecl,
                       this,
                       "Useless synthesized variable",
                       "Coding style issue (Facebook)",
                       "The @synthesize statement for property " + propName +
                           " appears to be useless.",
                       L);
  }

 public:
  // visiting property declarations in interfaces results in duplicated warnings
  // and false positives
  // so we use ObjCImplDecl as the entry point for scanning everything
  void checkASTDecl(const ObjCImplDecl *implDecl,
                    AnalysisManager &mgr,
                    BugReporter &BR) const {
    const ObjCInterfaceDecl *intDecl = implDecl->getClassInterface();
    assert(intDecl);

    // look for suspicious duplicated properties
    for (ObjCInterfaceDecl::prop_iterator PI = intDecl->prop_begin(),
                                          PE = intDecl->prop_end();
         PI != PE;
         PI++) {
      scanPropertyDecl(*PI, implDecl, mgr, BR);
    }
  }

  // look for suspicious or useless @synthesize
  void checkASTDecl(const ObjCPropertyImplDecl *propImplDecl,
                    AnalysisManager &mgr,
                    BugReporter &BR) const {
    const ObjCImplDecl *implDecl =
        dyn_cast<ObjCImplDecl>(propImplDecl->getDeclContext());
    assert(implDecl);
    scanPropertyImplDecl(propImplDecl, implDecl, mgr, BR);
  }
};

REGISTER_CHECKER_IN_PLUGIN(PropertySynthesizeChecker,
                           "facebook.PropertySynthesizeChecker",
                           "Check for duplicate properties and dangerous or "
                           "unnecessary synthesize statements")
