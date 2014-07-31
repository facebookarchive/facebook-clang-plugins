/**
 * Copyright (c) 2014, Facebook, Inc.
 * Copyright (c) 2003-2014 University of Illinois at Urbana-Champaign.
 * All rights reserved.
 *
 * This file is distributed under the University of Illinois Open Source License.
 * See LLVM-LICENSE for details.
 *
 */

/**
 * Clang frontend plugin to export an AST of clang into Json and Yojson (and ultimately Biniou)
 * while conforming to the inlined ATD specifications.
 *
 * /!\
 * '\atd' block comments are meant to be extracted and processed to generate ATD specifications for the Json dumper.
 * Do not modify ATD comments without modifying the Json emission accordingly (and conversely).
 * See ATD_GUIDELINES.md for more guidelines on how to write and test ATD annotations.
 *
 * This file was obtained by modifying the file ASTdumper.cpp from the LLVM/clang project.
 * The general layout should be maintained to make future merging easier.
 */

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/CommentVisitor.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclLookups.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/DeclVisitor.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/Basic/Module.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendDiagnostic.h>

#include <llvm/Support/raw_ostream.h>

#include "atdlib/ATDWriter.h"
#include "SimplePluginASTAction.h"
#include "FileUtils.h"

using namespace clang;
using namespace clang::comments;

//===----------------------------------------------------------------------===//
// ASTExporter Visitor
//===----------------------------------------------------------------------===//

namespace {

  typedef ATDWriter::JsonWriter<raw_ostream> JsonWriter;
  typedef ATDWriter::YojsonWriter<raw_ostream> YojsonWriter;

  template <class ATDWriter = YojsonWriter>
  class ASTExporter :
    public ConstDeclVisitor<ASTExporter<ATDWriter>>,
    public ConstStmtVisitor<ASTExporter<ATDWriter>>,
    public ConstCommentVisitor<ASTExporter<ATDWriter>>
  {
    typedef typename ATDWriter::ObjectScope ObjectScope;
    typedef typename ATDWriter::ArrayScope ArrayScope;
    typedef typename ATDWriter::TupleScope TupleScope;
    typedef typename ATDWriter::VariantScope VariantScope;
    ATDWriter OF;

    const CommandTraits &Traits;
    const SourceManager &SM;

    // Optional currentWorkingDirectory to normalize relative paths.
    StringRef BasePath;
    // Optional service to avoid repeating the content of a same header file across a compilation.
    FileUtils::DeduplicationService *DedupService;

    // Encoding of NULL pointers into suitable empty nodes
    // This is a hack but using option types in children lists would make the Json terribly verbose.
    // Also these useless nodes could have occurred in the original AST anyway :)
    //
    // Note: We are not using OwningPtr because 'delete' appears to be protected (at least on Stmt).
    const Stmt *const NullPtrStmt;
    const Decl *const NullPtrDecl;
    const Comment *const NullPtrComment;

    /// Keep track of the last location we print out so that we can
    /// print out deltas from then on out.
    const char *LastLocFilename;
    unsigned LastLocLine;

    /// The \c FullComment parent of the comment being dumped.
    const FullComment *FC;

  public:
    ASTExporter(raw_ostream &OS, ASTContext &Context, StringRef BasePath, FileUtils::DeduplicationService *DedupService)
      : OF(OS),
        Traits(Context.getCommentCommandTraits()),
        SM(Context.getSourceManager()),
        BasePath(BasePath),
        DedupService(DedupService),
        NullPtrStmt(new (Context) NullStmt(SourceLocation())),
        NullPtrDecl(EmptyDecl::Create(Context, Context.getTranslationUnitDecl(), SourceLocation())),
        NullPtrComment(new (Context) Comment(Comment::NoCommentKind, SourceLocation(), SourceLocation())),
        LastLocFilename(""), LastLocLine(~0U), FC(0)
    {
    }

    void dumpBareDecl(const Decl *D);
    void dumpBareStmt(const Stmt *S);
    void dumpFullComment(const FullComment *C);

    // Utilities
    void dumpBarePointer(const void *Ptr);
    void dumpBareSourceRange(SourceRange R);
    void dumpBareSourceLocation(SourceLocation Loc);
    void dumpBareQualType(QualType T);
    void dumpBareType(const Type *T);
    void dumpQualType(QualType T);
    void dumpBareDeclRef(const Decl &Node);
    void dumpDeclRef(const Decl *Node, const char *Label = 0);
    bool hasNodes(const DeclContext *DC);
    void dumpBareLookups(const DeclContext &DC);
    void dumpBareAttr(const Attr &A);
    void dumpBareSelector(const Selector sel);

    // C++ Utilities
    void dumpAccessSpecifier(AccessSpecifier AS);
    void dumpBareCXXCtorInitializer(const CXXCtorInitializer &Init);
//    void dumpTemplateParameters(const TemplateParameterList *TPL);
//    void dumpTemplateArgumentListInfo(const TemplateArgumentListInfo &TALI);
//    void dumpTemplateArgumentLoc(const TemplateArgumentLoc &A);
//    void dumpTemplateArgumentList(const TemplateArgumentList &TAL);
//    void dumpTemplateArgument(const TemplateArgument &A,
//                              SourceRange R = SourceRange());
    void dumpBareCXXBaseSpecifier(const CXXBaseSpecifier &Base);

    // Decls
    void VisitDecl(const Decl *D);
    void VisitDeclContext(const DeclContext *DC);
    void VisitBlockDecl(const BlockDecl *D);
    void VisitCapturedDecl(const CapturedDecl *D);
    void VisitLinkageSpecDecl(const LinkageSpecDecl *D);
    void VisitNamespaceDecl(const NamespaceDecl *D);
    void VisitObjCContainerDecl(const ObjCContainerDecl *D);
    void VisitTagDecl(const TagDecl *D);
    void VisitTypeDecl(const TypeDecl *D);
    void VisitTranslationUnitDecl(const TranslationUnitDecl *D);
    void VisitNamedDecl(const NamedDecl *D);
    void VisitValueDecl(const ValueDecl *D);
    void VisitTypedefDecl(const TypedefDecl *D);
    void VisitEnumDecl(const EnumDecl *D);
    void VisitRecordDecl(const RecordDecl *D);
    void VisitEnumConstantDecl(const EnumConstantDecl *D);
    void VisitIndirectFieldDecl(const IndirectFieldDecl *D);
    void VisitFunctionDecl(const FunctionDecl *D);
    void VisitFieldDecl(const FieldDecl *D);
    void VisitVarDecl(const VarDecl *D);
    void VisitFileScopeAsmDecl(const FileScopeAsmDecl *D);
    void VisitImportDecl(const ImportDecl *D);

//    // C++ Decls
//    void VisitNamespaceDecl(const NamespaceDecl *D);
//    void VisitUsingDirectiveDecl(const UsingDirectiveDecl *D);
//    void VisitNamespaceAliasDecl(const NamespaceAliasDecl *D);
//    void VisitTypeAliasDecl(const TypeAliasDecl *D);
//    void VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D);
//    void VisitCXXRecordDecl(const CXXRecordDecl *D);
//    void VisitStaticAssertDecl(const StaticAssertDecl *D);
//    void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D);
//    void VisitClassTemplateDecl(const ClassTemplateDecl *D);
//    void VisitClassTemplateSpecializationDecl(
//        const ClassTemplateSpecializationDecl *D);
//    void VisitClassTemplatePartialSpecializationDecl(
//        const ClassTemplatePartialSpecializationDecl *D);
//    void VisitClassScopeFunctionSpecializationDecl(
//        const ClassScopeFunctionSpecializationDecl *D);
//    void VisitVarTemplateDecl(const VarTemplateDecl *D);
//    void VisitVarTemplateSpecializationDecl(
//        const VarTemplateSpecializationDecl *D);
//    void VisitVarTemplatePartialSpecializationDecl(
//        const VarTemplatePartialSpecializationDecl *D);
//    void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D);
//    void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D);
//    void VisitTemplateTemplateParmDecl(const TemplateTemplateParmDecl *D);
//    void VisitUsingDecl(const UsingDecl *D);
//    void VisitUnresolvedUsingTypenameDecl(const UnresolvedUsingTypenameDecl *D);
//    void VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D);
//    void VisitUsingShadowDecl(const UsingShadowDecl *D);
//    void VisitLinkageSpecDecl(const LinkageSpecDecl *D);
//    void VisitAccessSpecDecl(const AccessSpecDecl *D);
//    void VisitFriendDecl(const FriendDecl *D);
//
//    // ObjC Decls
    void VisitObjCIvarDecl(const ObjCIvarDecl *D);
    void VisitObjCMethodDecl(const ObjCMethodDecl *D);
    void VisitObjCCategoryDecl(const ObjCCategoryDecl *D);
    void VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D);
    void VisitObjCProtocolDecl(const ObjCProtocolDecl *D);
    void VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D);
    void VisitObjCImplementationDecl(const ObjCImplementationDecl *D);
    void VisitObjCCompatibleAliasDecl(const ObjCCompatibleAliasDecl *D);
    void VisitObjCPropertyDecl(const ObjCPropertyDecl *D);
    void VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D);

    // Stmts.
    void VisitStmt(const Stmt *Node);
    void VisitDeclStmt(const DeclStmt *Node);
    void VisitAttributedStmt(const AttributedStmt *Node);
    void VisitLabelStmt(const LabelStmt *Node);
    void VisitGotoStmt(const GotoStmt *Node);
    void VisitCXXCatchStmt(const CXXCatchStmt *Node);

    // Exprs
    void VisitExpr(const Expr *Node);
    void VisitCastExpr(const CastExpr *Node);
    void VisitExplicitCastExpr(const ExplicitCastExpr *Node);
    void VisitDeclRefExpr(const DeclRefExpr *Node);
    void VisitPredefinedExpr(const PredefinedExpr *Node);
    void VisitCharacterLiteral(const CharacterLiteral *Node);
    void VisitIntegerLiteral(const IntegerLiteral *Node);
    void VisitFloatingLiteral(const FloatingLiteral *Node);
    void VisitStringLiteral(const StringLiteral *Str);
    void VisitUnaryOperator(const UnaryOperator *Node);
    void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *Node);
    void VisitMemberExpr(const MemberExpr *Node);
    void VisitExtVectorElementExpr(const ExtVectorElementExpr *Node);
    void VisitBinaryOperator(const BinaryOperator *Node);
    void VisitCompoundAssignOperator(const CompoundAssignOperator *Node);
    void VisitAddrLabelExpr(const AddrLabelExpr *Node);
    void VisitBlockExpr(const BlockExpr *Node);
    void VisitOpaqueValueExpr(const OpaqueValueExpr *Node);

    // C++
    void VisitCXXNamedCastExpr(const CXXNamedCastExpr *Node);
    void VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *Node);
    void VisitCXXConstructExpr(const CXXConstructExpr *Node);
    void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *Node);
    void VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *Node);
    void VisitExprWithCleanups(const ExprWithCleanups *Node);
//    void VisitUnresolvedLookupExpr(const UnresolvedLookupExpr *Node);
    void dumpBareCXXTemporary(const CXXTemporary *Temporary);
//    void VisitLambdaExpr(const LambdaExpr *Node) {
//      VisitExpr(Node);
//      dumpBareDecl(Node->getLambdaClass());
//    }

    // ObjC
    void VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node);
    void VisitObjCEncodeExpr(const ObjCEncodeExpr *Node);
    void VisitObjCMessageExpr(const ObjCMessageExpr *Node);
    void VisitObjCBoxedExpr(const ObjCBoxedExpr *Node);
    void VisitObjCSelectorExpr(const ObjCSelectorExpr *Node);
    void VisitObjCProtocolExpr(const ObjCProtocolExpr *Node);
    void VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *Node);
    void VisitObjCSubscriptRefExpr(const ObjCSubscriptRefExpr *Node);
    void VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node);
    void VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *Node);

// Comments.
    const char *getCommandName(unsigned CommandID);
    void dumpBareComment(const Comment *C);

    // Inline comments.
    void visitComment(const Comment *C);
    void visitTextComment(const TextComment *C);
//    void visitInlineCommandComment(const InlineCommandComment *C);
//    void visitHTMLStartTagComment(const HTMLStartTagComment *C);
//    void visitHTMLEndTagComment(const HTMLEndTagComment *C);
//
//    // Block comments.
//    void visitBlockCommandComment(const BlockCommandComment *C);
//    void visitParamCommandComment(const ParamCommandComment *C);
//    void visitTParamCommandComment(const TParamCommandComment *C);
//    void visitVerbatimBlockComment(const VerbatimBlockComment *C);
//    void visitVerbatimBlockLineComment(const VerbatimBlockLineComment *C);
//    void visitVerbatimLineComment(const VerbatimLineComment *C);
  };
}

//===----------------------------------------------------------------------===//
//  Utilities
//===----------------------------------------------------------------------===//

/// \atd
/// type pointer = string
template <class ATDWriter>
static void dumpBarePointer(ATDWriter &OF, const void *Ptr) {
  char str[20];
  snprintf(str, 20, "%p", Ptr);
  OF.emitString(std::string(str));
}

template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBarePointer(const void *Ptr) {
  ::dumpBarePointer(OF, Ptr);
}

/// \atd
/// type source_location = {
///   ?file : string option;
///   ?line : int option;
///   ?column : int option;
/// } <ocaml field_prefix="sl_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareSourceLocation(SourceLocation Loc) {
  ObjectScope Scope(OF);
  SourceLocation SpellingLoc = SM.getSpellingLoc(Loc);

  // The general format we print out is filename:line:col, but we drop pieces
  // that haven't changed since the last loc printed.
  PresumedLoc PLoc = SM.getPresumedLoc(SpellingLoc);

  if (PLoc.isInvalid()) {
    return;
  }

  if (strcmp(PLoc.getFilename(), LastLocFilename) != 0) {
    OF.emitTag("file");
    // Normalizing filenames matters because the current directory may change during the compilation of large projects.
    OF.emitString(FileUtils::normalizePath(BasePath, PLoc.getFilename()));
    OF.emitTag("line");
    OF.emitInteger(PLoc.getLine());
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  } else if (PLoc.getLine() != LastLocLine) {
    OF.emitTag("line");
    OF.emitInteger(PLoc.getLine());
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  } else {
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  }
  LastLocFilename = PLoc.getFilename();
  LastLocLine = PLoc.getLine();
  // TODO: lastLocColumn
}

/// \atd
/// type source_range = (source_location * source_location)
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareSourceRange(SourceRange R) {
  TupleScope Scope(OF);
  dumpBareSourceLocation(R.getBegin());
  dumpBareSourceLocation(R.getEnd());
}

// TODO: really dump types as trees
/// \atd
/// type opt_type = [Type of string | NoType]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareType(const Type *T) {
  if (!T) {
    OF.emitSimpleVariant("NoType");
  } else {
    VariantScope Scope(OF, "Type");
    OF.emitString(QualType::getAsString(QualType(T, 0).getSplitDesugaredType()));
  }
}

/// \atd
/// type qual_type = {
///   raw : string;
///   ?desugared : string option
/// } <ocaml field_prefix="qt_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareQualType(QualType T) {
  ObjectScope Scope(OF);
  SplitQualType T_split = T.split();
  OF.emitTag("raw");
  OF.emitString(QualType::getAsString(T_split));
  if (!T.isNull()) {
    // If the type is sugared, also dump a (shallow) desugared type.
    SplitQualType D_split = T.getSplitDesugaredType();
    if (T_split != D_split) {
      OF.emitTag("desugared");
      OF.emitString(QualType::getAsString(D_split));
    }
  }
}

/// \atd
/// type _qual_type = { qual_type : qual_type }
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpQualType(QualType T) {
  OF.emitTag("qual_type");
  dumpBareQualType(T);
}

/// \atd
/// type decl_ref = {
///   kind : decl_kind;
///   ?name : string option;
///   ~is_hidden : bool;
///   ?qual_type : qual_type option
/// } <ocaml field_prefix="dr_">
///
/// type decl_kind = [
#define DECL(DERIVED, BASE) /// | DERIVED
#define ABSTRACT_DECL(DECL) DECL
#include <clang/AST/DeclNodes.inc>
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareDeclRef(const Decl &D) {
  ObjectScope Scope(OF);
  OF.emitTag("kind");
  OF.emitSimpleVariant(D.getDeclKindName());
  const NamedDecl *ND = dyn_cast<NamedDecl>(&D);
  if (ND) {
    OF.emitTag("name");
    OF.emitString(ND->getNameAsString());
    OF.emitFlag("is_hidden", ND->isHidden());
  }
  if (const ValueDecl *VD = dyn_cast<ValueDecl>(&D)) {
    dumpQualType(VD->getType());
  }
}

/// \atd
/// type _decl_ref = { ?decl_ref : decl_ref option }
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpDeclRef(const Decl *D, const char *Label) {
  if (!D)
    return;
  
  if (Label) {
    // ATD TODO: not supported
    OF.emitTag(Label);
  } else {
    OF.emitTag("decl_ref");
  }
  dumpBareDeclRef(*D);
}

/// \atd
/// #define decl_context_tuple decl list * decl_context_info
/// type decl_context_info = {
///   ~has_external_lexical_storage : bool;
///   ~has_external_visible_storage : bool
/// } <ocaml field_prefix="dci_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDeclContext(const DeclContext *DC) {
  if (!DC) {
    { ArrayScope Scope(OF); }
    { ObjectScope Scope(OF); }
    return;
  }
  {
    ArrayScope Scope(OF);
    for (DeclContext::decl_iterator I = DC->decls_begin(), E = DC->decls_end();
         I != E; ++I) {
      if (!DedupService || DedupService->verifyDeclFileLocation(**I)) {
        dumpBareDecl(*I);
      }
    }
  }
  {
    ObjectScope Scope(OF);
    OF.emitFlag("has_external_lexical_storage", DC->hasExternalLexicalStorage());
    OF.emitFlag("has_external_visible_storage", DC->hasExternalVisibleStorage());
  }
}

/// \atd
/// type lookups = {
///   decl_ref : decl_ref;
///   ?primary_context_pointer : pointer option;
///   lookups : lookup list;
///   ~has_undeserialized_decls : bool;
/// } <ocaml field_prefix="lups_">
///
/// type lookup = {
///   decl_name : string;
///   decl_refs : decl_ref list;
/// } <ocaml field_prefix="lup_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareLookups(const DeclContext &DC) {
  ObjectScope Scope(OF);

  OF.emitTag("decl_ref");
  dumpBareDeclRef(cast<Decl>(DC));

  const DeclContext *Primary = DC.getPrimaryContext();
  if (Primary != &DC) {
    OF.emitTag("primary_context_pointer");
    dumpBarePointer(cast<Decl>(Primary));
  }

  OF.emitTag("lookups");
  {
    ArrayScope Scope(OF);
    DeclContext::all_lookups_iterator I = Primary->noload_lookups_begin(),
    E = Primary->noload_lookups_end();
    while (I != E) {
      DeclarationName Name = I.getLookupName();
      DeclContextLookupResult R = *I++;

      ObjectScope Scope(OF);
      OF.emitTag("decl_name");
      OF.emitString(Name.getAsString());

      OF.emitTag("decl_refs");
      {
        ArrayScope Scope(OF);
        for (DeclContextLookupResult::iterator RI = R.begin(), RE = R.end();
             RI != RE; ++RI) {
          dumpBareDeclRef(**RI);
        }
      }
    }
  }

  bool HasUndeserializedLookups = Primary->hasExternalVisibleStorage();
  OF.emitFlag("has_undeserialized_decls", HasUndeserializedLookups);
}

//TODO: dump the content of the attributes
//static bool hasMoreChildren() { return false; }
//static void setMoreChildren(bool x) {}
//static void lastChild() {}

/// \atd
/// type attribute = [
#define ATTR(X) ///   | X@@Attr of attribute_info
#include <clang/Basic/AttrList.inc>
/// ] <ocaml repr="classic">
/// type attribute_info = {
///   pointer : pointer;
///   source_range : source_range;
///   ~is_implicit : bool
/// } <ocaml field_prefix="ai_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareAttr(const Attr &A) {
  std::string tag;
  switch (A.getKind()) {
#define ATTR(X) case attr::X: tag = #X "Attr"; break;
#include <clang/Basic/AttrList.inc>
  default: llvm_unreachable("unexpected attribute kind");
  }
  VariantScope Scope(OF, tag);
  {
    ObjectScope Scope(OF);
    OF.emitTag("pointer");
    dumpBarePointer(&A);
    OF.emitTag("source_range");
    dumpBareSourceRange(A.getRange());

// TODO
//#include <clang/AST/AttrDump.inc>
    OF.emitFlag("is_implicit", A.isImplicit());
  }
}

/// \atd
/// type previous_decl = [
/// | None
/// | First of pointer
/// | Previous of pointer
/// ]
template <class ATDWriter>
static void dumpPreviousDeclImpl(ATDWriter &OF, ...) {}

template <class ATDWriter, typename T>
static void dumpPreviousDeclImpl(ATDWriter &OF, const Mergeable<T> *D) {
  const T *First = D->getFirstDecl();
  if (First != D) {
    OF.emitTag("previous_decl");
    typename ATDWriter::VariantScope Scope(OF, "First");
    dumpBarePointer(OF, First);
  }
}

template <class ATDWriter, typename T>
static void dumpPreviousDeclImpl(ATDWriter &OF, const Redeclarable<T> *D) {
  const T *Prev = D->getPreviousDecl();
  if (Prev) {
    OF.emitTag("previous_decl");
    typename ATDWriter::VariantScope Scope(OF, "Previous");
    dumpBarePointer(OF, Prev);
  }
}

/// Dump the previous declaration in the redeclaration chain for a declaration,
/// if any.
///
/// \atd type _previous_decl = {
/// ~previous_decl <ocaml default="`None"> : previous_decl;
/// }
template <class ATDWriter>
static void dumpPreviousDecl(ATDWriter &OF, const Decl *D) {
  switch (D->getKind()) {
#define DECL(DERIVED, BASE) \
  case Decl::DERIVED: \
    return dumpPreviousDeclImpl(OF, cast<DERIVED##Decl>(D));
#define ABSTRACT_DECL(DECL)
#include <clang/AST/DeclNodes.inc>
  }
  llvm_unreachable("Decl that isn't part of DeclNodes.inc!");
}

//===----------------------------------------------------------------------===//
//  C++ Utilities
//===----------------------------------------------------------------------===//

/// \atd
/// type _access_specifier = { ~access_specifier <ocaml default="`None"> : access_specifier }
/// type access_specifier = [ None | Public | Protected | Private ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpAccessSpecifier(AccessSpecifier AS) {
  if (AS == AS_none) {
    return;
  }
  OF.emitTag("access_specifier");
  switch (AS) {
  case AS_public:
    OF.emitSimpleVariant("Public");
    break;
  case AS_protected:
    OF.emitSimpleVariant("Protected");
    break;
  case AS_private:
    OF.emitSimpleVariant("Private");
    break;
  default:
    llvm_unreachable("unknown case");
    break;
  }
}

/// \atd
/// type cxx_ctor_initializer = {
///   subject : cxx_ctor_initializer_subject;
///   ?init_expr : stmt option
/// } <ocaml field_prefix="xci_">
/// type cxx_ctor_initializer_subject = [
///   Member of decl_ref
/// | Delegating of qual_type
/// | BaseClass of (qual_type * bool)
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareCXXCtorInitializer(const CXXCtorInitializer &Init) {
  ObjectScope Scope(OF);
  OF.emitTag("subject");
  const FieldDecl *FD = Init.getAnyMember();
  if (FD) {
    VariantScope Scope(OF, "Member");
    dumpBareDeclRef(*FD);
  } else if (Init.isDelegatingInitializer()) {
    VariantScope Scope(OF, "Delegating");
    dumpBareQualType(Init.getTypeSourceInfo()->getType());
  } else {
    VariantScope Scope(OF, "BaseClass");
    {
      TupleScope Scope(OF);
      dumpBareQualType(Init.getTypeSourceInfo()->getType());
      OF.emitBoolean(Init.isBaseVirtual());
    }
  }
  const Expr *E = Init.getInit();
  if (E) {
    OF.emitTag("init_expr");
    dumpBareStmt(E);
  }
}

//template <class ATDWriter>
//void ASTExporter<ATDWriter>::dumpTemplateParameters(const TemplateParameterList *TPL) {
//  if (!TPL)
//    return;
//
//  for (TemplateParameterList::const_iterator I = TPL->begin(), E = TPL->end();
//       I != E; ++I)
//    dumpBareDecl(*I);
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::dumpTemplateArgumentListInfo(
//    const TemplateArgumentListInfo &TALI) {
//  for (unsigned i = 0, e = TALI.size(); i < e; ++i) {
//    dumpTemplateArgumentLoc(TALI[i]);
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::dumpTemplateArgumentLoc(const TemplateArgumentLoc &A) {
//  dumpTemplateArgument(A.getArgument(), A.getSourceRange());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::dumpTemplateArgumentList(const TemplateArgumentList &TAL) {
//  for (unsigned i = 0, e = TAL.size(); i < e; ++i)
//    dumpTemplateArgument(TAL[i]);
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::dumpTemplateArgument(const TemplateArgument &A, SourceRange R) {
//  ObjectScope Scope(OF);
//  OS << "TemplateArgument";
//  if (R.isValid())
//    dumpSourceRange(R);
//
//  switch (A.getKind()) {
//  case TemplateArgument::Null:
//    OS << " null";
//    break;
//  case TemplateArgument::Type:
//    OS << " type";
//    dumpQualType(A.getAsType());
//    break;
//  case TemplateArgument::Declaration:
//    OS << " decl";
//    dumpDeclRef(A.getAsDecl());
//    break;
//  case TemplateArgument::NullPtr:
//    OS << " nullptr";
//    break;
//  case TemplateArgument::Integral:
//    OS << " integral " << A.getAsIntegral();
//    break;
//  case TemplateArgument::Template:
//    OS << " template ";
//    // FIXME: do not use the local dump method
//    A.getAsTemplate().dump(OS);
//    break;
//  case TemplateArgument::TemplateExpansion:
//    OS << " template expansion";
//    // FIXME: do not use the local dump method
//    A.getAsTemplateOrTemplatePattern().dump(OS);
//    break;
//  case TemplateArgument::Expression:
//    OS << " expr";
//    dumpBareStmt(A.getAsExpr());
//    break;
//  case TemplateArgument::Pack:
//    OS << " pack";
//    for (TemplateArgument::pack_iterator I = A.pack_begin(), E = A.pack_end();
//         I != E; ++I) {
//      dumpTemplateArgument(*I);
//    }
//    break;
//  }
//}

//===----------------------------------------------------------------------===//
//  Decl dumping methods.
//===----------------------------------------------------------------------===//

/// \atd
#define DECL(DERIVED, BASE) /// #define @DERIVED@_decl_tuple @BASE@_tuple
#define ABSTRACT_DECL(DECL) DECL
#include <clang/AST/DeclNodes.inc>
///
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareDecl(const Decl *D) {
  if (!D) {
    // We use a fixed EmptyDecl node to represent null pointers
    D = NullPtrDecl;
  }
  VariantScope Scope(OF, std::string(D->getDeclKindName()) + "Decl");
  {
    TupleScope Scope(OF);
    ConstDeclVisitor<ASTExporter<ATDWriter>>::Visit(D);
  }
}

/// \atd
/// #define decl_tuple decl_info
/// type decl_info = {
///   pointer : pointer;
///   ?parent_pointer : pointer option;
///   inherit _previous_decl;
///   source_range : source_range;
///   ?owning_module : string option;
///   ~is_hidden : bool;
///   attributes : attribute list;
///   inherit _full_comment;
///   ~is_invalid_decl : bool
/// } <ocaml field_prefix="di_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDecl(const Decl *D) {
  {
    ObjectScope Scope(OF);

    OF.emitTag("pointer");
    dumpBarePointer(D);
    if (D->getLexicalDeclContext() != D->getDeclContext()) {
      OF.emitTag("parent_pointer");
      dumpBarePointer(cast<Decl>(D->getDeclContext()));
    }
    dumpPreviousDecl(OF, D);
    OF.emitTag("source_range");
    dumpBareSourceRange(D->getSourceRange());
    if (Module *M = D->getOwningModule()) {
      OF.emitTag("owning_module");
      OF.emitString(M->getFullModuleName());
    }
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
      OF.emitFlag("is_hidden", ND->isHidden());
    }

    OF.emitTag("attributes");
    {
      ArrayScope ArrayAttr(OF);
      for (Decl::attr_iterator I = D->attr_begin(), E = D->attr_end();
           I != E; ++I) {
        assert(*I);
        dumpBareAttr(**I);
      }
    }

    const FullComment *Comment = D->getASTContext().getLocalCommentForDeclUncached(D);
    dumpFullComment(Comment);

    OF.emitFlag("is_invalid_decl", D->isInvalidDecl());
  }
}

/// \atd
/// #define captured_decl_tuple decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCapturedDecl(const CapturedDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
}

/// \atd
/// #define linkage_spec_decl_tuple decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitLinkageSpecDecl(const LinkageSpecDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
}

/// \atd
/// #define namespace_decl_tuple named_decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitNamespaceDecl(const NamespaceDecl *D) {
  VisitNamedDecl(D);
  VisitDeclContext(D);
}

/// \atd
/// #define obj_c_container_decl_tuple named_decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCContainerDecl(const ObjCContainerDecl *D) {
  VisitNamedDecl(D);
  VisitDeclContext(D);
}

/// \atd
/// #define tag_decl_tuple type_decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTagDecl(const TagDecl *D) {
  VisitTypeDecl(D);
  VisitDeclContext(D);
}

/// \atd
/// #define type_decl_tuple named_decl_tuple * opt_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTypeDecl(const TypeDecl *D) {
  VisitNamedDecl(D);
  dumpBareType(D->getTypeForDecl());
}

/// \atd
/// #define value_decl_tuple named_decl_tuple * qual_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitValueDecl(const ValueDecl *D) {
  VisitNamedDecl(D);
  dumpBareQualType(D->getType());
}


/// \atd
/// #define translation_unit_decl_tuple decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTranslationUnitDecl(const TranslationUnitDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
}

/// \atd
/// #define named_decl_tuple decl_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitNamedDecl(const NamedDecl *D) {
  VisitDecl(D);
  OF.emitString(D->getNameAsString());
}

/// \atd
/// #define typedef_decl_tuple typedef_name_decl_tuple * typedef_decl_info
/// type typedef_decl_info = {
///   ~is_module_private : bool
/// } <ocaml field_prefix="tdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTypedefDecl(const TypedefDecl *D) {
  ASTExporter<ATDWriter>::VisitTypedefNameDecl(D);
  ObjectScope Scope(OF);
  OF.emitFlag("is_module_private", D->isModulePrivate());
}

/// \atd
/// #define enum_decl_tuple tag_decl_tuple * enum_decl_info
/// type enum_decl_info = {
///   ?scope : enum_decl_scope option;
///   ~is_module_private : bool
/// } <ocaml field_prefix="edi_">
/// type enum_decl_scope = [Class | Struct]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitEnumDecl(const EnumDecl *D) {
  VisitTagDecl(D);
  ObjectScope Scope(OF);
  if (D->isScoped()) {
    OF.emitTag("scope");
    if (D->isScopedUsingClassTag())
      OF.emitSimpleVariant("Class");
    else
      OF.emitSimpleVariant("Struct");
  }
  OF.emitFlag("is_module_private", D->isModulePrivate());
}

/// \atd
/// #define record_decl_tuple tag_decl_tuple * record_decl_info
/// type record_decl_info = {
///   ~is_module_private : bool;
///   ~is_complete_definition : bool
/// } <ocaml field_prefix="rdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitRecordDecl(const RecordDecl *D) {
  VisitTagDecl(D);
  ObjectScope Scope(OF);
  OF.emitFlag("is_module_private", D->isModulePrivate());
  OF.emitFlag("is_complete_definition", D->isCompleteDefinition());
}

/// \atd
/// #define enum_constant_decl_tuple value_decl_tuple * enum_constant_decl_info
/// type enum_constant_decl_info = {
///   ?init_expr : stmt option
/// } <ocaml field_prefix="ecdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitEnumConstantDecl(const EnumConstantDecl *D) {
  VisitValueDecl(D);
  ObjectScope Scope(OF);
  if (const Expr *Init = D->getInitExpr()) {
    OF.emitTag("init_expr");
    dumpBareStmt(Init);
  }
}

/// \atd
/// #define indirect_field_decl_tuple value_decl_tuple * decl_ref list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitIndirectFieldDecl(const IndirectFieldDecl *D) {
  VisitValueDecl(D);
  ArrayScope Scope(OF);
  for (IndirectFieldDecl::chain_iterator I = D->chain_begin(),
                                         E = D->chain_end();
       I != E; ++I) {
    assert(*I);
    dumpBareDeclRef(**I);
  }
}

/// \atd
/// #define function_decl_tuple declarator_decl_tuple * function_decl_info
/// type function_decl_info = {
///   ?storage_class : string option;
///   ~is_inline : bool;
///   ~is_virtual : bool;
///   ~is_module_private : bool;
///   ~is_pure : bool;
///   ~is_delete_as_written : bool;
///   ~decls_in_prototype_scope : decl list;
///   ~parameters : decl list;
///   ~cxx_ctor_initializers : cxx_ctor_initializer list;
///   ?body : stmt option
/// } <ocaml field_prefix="fdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFunctionDecl(const FunctionDecl *D) {
  ASTExporter<ATDWriter>::VisitDeclaratorDecl(D);
  // We purposedly do not call VisitDeclContext(D).
  ObjectScope Scope(OF);

  StorageClass SC = D->getStorageClass();
  if (SC != SC_None) {
    OF.emitTag("storage_class");
    OF.emitString(VarDecl::getStorageClassSpecifierString(SC));
  }

  OF.emitFlag("is_inline", D->isInlineSpecified());
  OF.emitFlag("is_virtual", D->isVirtualAsWritten());
  OF.emitFlag("is_module_private", D->isModulePrivate());
  OF.emitFlag("is_pure", D->isPure());
  OF.emitFlag("is_delete_as_written", D->isDeletedAsWritten());

//  if (const FunctionProtoType *FPT = D->getType()->getAs<FunctionProtoType>()) {
//    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
//    switch (EPI.ExceptionSpecType) {
//    default: break;
//    case EST_Unevaluated:
//      OS << " noexcept-unevaluated " << EPI.ExceptionSpecDecl;
//      break;
//    case EST_Uninstantiated:
//      OS << " noexcept-uninstantiated " << EPI.ExceptionSpecTemplate;
//      break;
//    }
//  }
//
//  const FunctionTemplateSpecializationInfo *FTSI =
//      D->getTemplateSpecializationInfo();
//  bool HasTemplateSpecialization = FTSI;
//
//
//  if (HasTemplateSpecialization) {
//    dumpTemplateArgumentList(*FTSI->TemplateArguments);
//  }

  {
    ArrayRef<NamedDecl *>::iterator
    I = D->getDeclsInPrototypeScope().begin(),
    E = D->getDeclsInPrototypeScope().end();
    if (I != E) {
      OF.emitTag("decls_in_prototype_scope");
      ArrayScope Scope(OF);
      for (; I != E; ++I) {
        dumpBareDecl(*I);
      }
    }
  }

  {
    FunctionDecl::param_const_iterator I = D->param_begin(), E = D->param_end();
    if (I != E) {
      OF.emitTag("parameters");
      ArrayScope Scope(OF);
      for (; I != E; ++I) {
        dumpBareDecl(*I);
      }
    }
  }

  const CXXConstructorDecl *C = dyn_cast<CXXConstructorDecl>(D);
  bool HasCtorInitializers = C && C->init_begin() != C->init_end();
  if (HasCtorInitializers) {
    OF.emitTag("cxx_ctor_initializers");
    ArrayScope Scope(OF);
    for (CXXConstructorDecl::init_const_iterator I = C->init_begin(),
         E = C->init_end();
         I != E; ++I) {
      assert(*I);
      dumpBareCXXCtorInitializer(**I);
    }
  }

  bool HasDeclarationBody = D->doesThisDeclarationHaveABody();
  if (HasDeclarationBody) {
    const Stmt *Body = D->getBody();
    if (Body) {
      OF.emitTag("body");
      dumpBareStmt(Body);
    }
  }
}

/// \atd
/// #define field_decl_tuple declarator_decl_tuple * field_decl_info
/// type field_decl_info = {
///   ~is_mutable : bool;
///   ~is_module_private : bool;
///   ?init_expr : stmt option;
///   ?bit_width_expr : stmt option
/// } <ocaml field_prefix="fldi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFieldDecl(const FieldDecl *D) {
  ASTExporter<ATDWriter>::VisitDeclaratorDecl(D);
  ObjectScope Scope(OF);

  OF.emitFlag("is_mutable", D->isMutable());
  OF.emitFlag("is_module_private", D->isModulePrivate());

  bool IsBitField = D->isBitField();
  if (IsBitField && D->getBitWidth()) {
    OF.emitTag("bit_width_expr");
    dumpBareStmt(D->getBitWidth());
  }

  Expr *Init = D->getInClassInitializer();
  if (Init) {
    OF.emitTag("init_expr");
    dumpBareStmt(Init);
  }
}

/// \atd
/// #define var_decl_tuple declarator_decl_tuple * var_decl_info
/// type var_decl_info = {
///   ?storage_class : string option;
///   ~tls_kind <ocaml default="`Tls_none">: tls_kind;
///   ~is_module_private : bool;
///   ~is_nrvo_variable : bool;
///   ?init_expr : stmt option;
/// } <ocaml field_prefix="vdi_">
///
/// type tls_kind = [ Tls_none | Tls_static | Tls_dynamic ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitVarDecl(const VarDecl *D) {
  ASTExporter<ATDWriter>::VisitDeclaratorDecl(D);
  ObjectScope Scope(OF);

  StorageClass SC = D->getStorageClass();
  if (SC != SC_None) {
    OF.emitTag("storage_class");
    OF.emitString(VarDecl::getStorageClassSpecifierString(SC));
  }

  switch (D->getTLSKind()) {
  case VarDecl::TLS_None: break;
  case VarDecl::TLS_Static: OF.emitTag("tls_kind"); OF.emitSimpleVariant("Tls_static"); break;
  case VarDecl::TLS_Dynamic: OF.emitTag("tls_kind"); OF.emitSimpleVariant("Tls_dynamic"); break;
  }

  OF.emitFlag("is_module_private", D->isModulePrivate());
  OF.emitFlag("is_nrvo_variable", D->isNRVOVariable());
  if (D->hasInit()) {
    OF.emitTag("init_expr");
    dumpBareStmt(D->getInit());
  }
}

/// \atd
/// #define file_scope_asm_decl_tuple decl_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFileScopeAsmDecl(const FileScopeAsmDecl *D) {
  VisitDecl(D);
  OF.emitString(D->getAsmString()->getBytes());
}

/// \atd
/// #define import_decl_tuple decl_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitImportDecl(const ImportDecl *D) {
  VisitDecl(D);
  OF.emitString(D->getImportedModule()->getFullModuleName());
}

////===----------------------------------------------------------------------===//
//// C++ Declarations
////===----------------------------------------------------------------------===//
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitNamespaceDecl(const NamespaceDecl *D) {
//  dumpName(D);
//  if (D->isInline())
//    OS << " inline";
//  if (!D->isOriginalNamespace())
//    dumpDeclRef(D->getOriginalNamespace(), "original");
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitUsingDirectiveDecl(const UsingDirectiveDecl *D) {
//  OS << ' ';
//  dumpBareDeclRef(D->getNominatedNamespace());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitNamespaceAliasDecl(const NamespaceAliasDecl *D) {
//  dumpName(D);
//  dumpDeclRef(D->getAliasedNamespace());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitTypeAliasDecl(const TypeAliasDecl *D) {
//  dumpName(D);
//  dumpQualType(D->getUnderlyingType());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//  dumpBareDecl(D->getTemplatedDecl());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitCXXRecordDecl(const CXXRecordDecl *D) {
//  VisitRecordDecl(D);
//  if (!D->isCompleteDefinition())
//    return;
//
//  for (CXXRecordDecl::base_class_const_iterator I = D->bases_begin(),
//                                                E = D->bases_end();
//       I != E; ++I) {
//    ObjectScope Scope(OF);
//    if (I->isVirtual())
//      OS << "virtual ";
//    dumpAccessSpecifier(I->getAccessSpecifier());
//    dumpQualType(I->getType());
//    if (I->isPackExpansion())
//      OS << "...";
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitStaticAssertDecl(const StaticAssertDecl *D) {
//  dumpBareStmt(D->getAssertExpr());
//  dumpBareStmt(D->getMessage());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//  dumpBareDecl(D->getTemplatedDecl());
//  for (FunctionTemplateDecl::spec_iterator I = D->spec_begin(),
//                                           E = D->spec_end();
//       I != E; ++I) {
//    FunctionTemplateDecl::spec_iterator Next = I;
//    ++Next;
//    switch (I->getTemplateSpecializationKind()) {
//    case TSK_Undeclared:
//    case TSK_ImplicitInstantiation:
//    case TSK_ExplicitInstantiationDeclaration:
//    case TSK_ExplicitInstantiationDefinition:
//      if (D == D->getCanonicalDecl())
//        dumpBareDecl(*I);
//      else
//        dumpDeclRef(*I);
//      break;
//    case TSK_ExplicitSpecialization:
//      dumpDeclRef(*I);
//      break;
//    }
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitClassTemplateDecl(const ClassTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//
//  ClassTemplateDecl::spec_iterator I = D->spec_begin();
//  ClassTemplateDecl::spec_iterator E = D->spec_end();
//  dumpBareDecl(D->getTemplatedDecl());
//  for (; I != E; ++I) {
//    ClassTemplateDecl::spec_iterator Next = I;
//    ++Next;
//    switch (I->getTemplateSpecializationKind()) {
//    case TSK_Undeclared:
//    case TSK_ImplicitInstantiation:
//      if (D == D->getCanonicalDecl())
//        dumpBareDecl(*I);
//      else
//        dumpDeclRef(*I);
//      break;
//    case TSK_ExplicitSpecialization:
//    case TSK_ExplicitInstantiationDeclaration:
//    case TSK_ExplicitInstantiationDefinition:
//      dumpDeclRef(*I);
//      break;
//    }
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitClassTemplateSpecializationDecl(
//    const ClassTemplateSpecializationDecl *D) {
//  VisitCXXRecordDecl(D);
//  dumpTemplateArgumentList(D->getTemplateArgs());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitClassTemplatePartialSpecializationDecl(
//    const ClassTemplatePartialSpecializationDecl *D) {
//  VisitClassTemplateSpecializationDecl(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitClassScopeFunctionSpecializationDecl(
//    const ClassScopeFunctionSpecializationDecl *D) {
//  dumpDeclRef(D->getSpecialization());
//  if (D->hasExplicitTemplateArgs())
//    dumpTemplateArgumentListInfo(D->templateArgs());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitVarTemplateDecl(const VarTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//
//  VarTemplateDecl::spec_iterator I = D->spec_begin();
//  VarTemplateDecl::spec_iterator E = D->spec_end();
//  dumpBareDecl(D->getTemplatedDecl());
//  for (; I != E; ++I) {
//    VarTemplateDecl::spec_iterator Next = I;
//    ++Next;
//    switch (I->getTemplateSpecializationKind()) {
//    case TSK_Undeclared:
//    case TSK_ImplicitInstantiation:
//      if (D == D->getCanonicalDecl())
//        dumpBareDecl(*I);
//      else
//        dumpDeclRef(*I);
//      break;
//    case TSK_ExplicitSpecialization:
//    case TSK_ExplicitInstantiationDeclaration:
//    case TSK_ExplicitInstantiationDefinition:
//      dumpDeclRef(*I);
//      break;
//    }
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitVarTemplateSpecializationDecl(
//    const VarTemplateSpecializationDecl *D) {
//  dumpTemplateArgumentList(D->getTemplateArgs());
//  VisitVarDecl(D);
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitVarTemplatePartialSpecializationDecl(
//    const VarTemplatePartialSpecializationDecl *D) {
//  dumpTemplateParameters(D->getTemplateParameters());
//  VisitVarTemplateSpecializationDecl(D);
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
//  if (D->wasDeclaredWithTypename())
//    OS << " typename";
//  else
//    OS << " class";
//  if (D->isParameterPack())
//    OS << " ...";
//  dumpName(D);
//  if (D->hasDefaultArgument())
//    dumpQualType(D->getDefaultArgument());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D) {
//  dumpQualType(D->getType());
//  if (D->isParameterPack())
//    OS << " ...";
//  dumpName(D);
//  if (D->hasDefaultArgument())
//    dumpBareStmt(D->getDefaultArgument());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitTemplateTemplateParmDecl(
//    const TemplateTemplateParmDecl *D) {
//  if (D->isParameterPack())
//    OS << " ...";
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//  if (D->hasDefaultArgument())
//    dumpTemplateArgumentLoc(D->getDefaultArgument());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitUsingDecl(const UsingDecl *D) {
//  OS << ' ';
//  D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
//  OS << D->getNameAsString();
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitUnresolvedUsingTypenameDecl(
//    const UnresolvedUsingTypenameDecl *D) {
//  OS << ' ';
//  D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
//  OS << D->getNameAsString();
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D) {
//  OS << ' ';
//  D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
//  OS << D->getNameAsString();
//  dumpQualType(D->getType());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitUsingShadowDecl(const UsingShadowDecl *D) {
//  OS << ' ';
//  dumpBareDeclRef(D->getTargetDecl());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitLinkageSpecDecl(const LinkageSpecDecl *D) {
//  switch (D->getLanguage()) {
//  case LinkageSpecDecl::lang_c: OS << " C"; break;
//  case LinkageSpecDecl::lang_cxx: OS << " C++"; break;
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitAccessSpecDecl(const AccessSpecDecl *D) {
//  OS << ' ';
//  dumpAccessSpecifier(D->getAccess());
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitFriendDecl(const FriendDecl *D) {
//  if (TypeSourceInfo *T = D->getFriendType())
//    dumpQualType(T->getType());
//  else
//    dumpBareDecl(D->getFriendDecl());
//}
//
////===----------------------------------------------------------------------===//
//// Obj-C Declarations
////===----------------------------------------------------------------------===//

/// \atd
/// #define obj_c_ivar_decl_tuple field_decl_tuple * obj_c_ivar_decl_info
/// type obj_c_ivar_decl_info = {
///   ~is_synthesize : bool;
///   ~access_control <ocaml default="`None"> : obj_c_access_control;
/// } <ocaml field_prefix="ovdi_">
/// type obj_c_access_control = [ None | Private | Protected | Public | Package ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCIvarDecl(const ObjCIvarDecl *D) {
  VisitFieldDecl(D);
  ObjectScope Scope(OF);
  OF.emitFlag("is_synthesize", D->getSynthesize());

  ObjCIvarDecl::AccessControl AC = D->getAccessControl();
  if (AC != ObjCIvarDecl::None) {
    OF.emitTag("access_control");
    switch (AC) {
      case ObjCIvarDecl::Private:
        OF.emitSimpleVariant("Private");
        break;
      case ObjCIvarDecl::Protected:
        OF.emitSimpleVariant("Protected");
        break;
      case ObjCIvarDecl::Public:
        OF.emitSimpleVariant("Public");
        break;
      case ObjCIvarDecl::Package:
        OF.emitSimpleVariant("Package");
        break;
      default:
        llvm_unreachable("unknown case");
        break;
    }
  }
}

/// \atd
/// #define obj_c_method_decl_tuple named_decl_tuple * obj_c_method_decl_info
/// type obj_c_method_decl_info = {
///   ~is_instance_method : bool;
///   result_type : qual_type;
///   ~parameters : decl list;
///   ~is_variadic : bool;
///   ?body : stmt option;
/// } <ocaml field_prefix="omdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCMethodDecl(const ObjCMethodDecl *D) {
  VisitNamedDecl(D);
  // We purposedly do not call VisitDeclContext(D).
  ObjectScope Scope(OF);
  OF.emitFlag("is_instance_method", D->isInstanceMethod());
  OF.emitTag("result_type");
  dumpBareQualType(D->getReturnType());
  {
    ObjCMethodDecl::param_const_iterator I = D->param_begin(), E = D->param_end();
    if (I != E) {
      OF.emitTag("parameters");
      ArrayScope Scope(OF);
      for (; I != E; ++I) {
        dumpBareDecl(*I);
      }
    }
  }
  OF.emitFlag("is_variadic", D->isVariadic());

  const Stmt *Body = D->getBody();
  if (Body) {
    OF.emitTag("body");
    dumpBareStmt(Body);
  }
}

/// \atd
/// #define obj_c_category_decl_tuple obj_c_container_decl_tuple * obj_c_category_decl_info
/// type obj_c_category_decl_info = {
///   ?class_interface : decl_ref option;
///   ?implementation : decl_ref option;
///   ~protocols : decl_ref list;
/// } <ocaml field_prefix="odi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
  VisitObjCContainerDecl(D);
  ObjectScope Scope(OF);
  const ObjCInterfaceDecl *CI = D->getClassInterface();
  if (CI) {
    OF.emitTag("class_interface");
    dumpBareDeclRef(*CI);
  }
  const ObjCCategoryImplDecl *Impl = D->getImplementation();
  if (Impl) {
    OF.emitTag("implementation");
    dumpBareDeclRef(*Impl);
  }
  ObjCCategoryDecl::protocol_iterator I = D->protocol_begin(),
  E = D->protocol_end();
  if (I != E) {
    OF.emitTag("protocols");
    ArrayScope Scope(OF);
    for (; I != E; ++I) {
      assert(*I);
      dumpBareDeclRef(**I);
    }
  }
}

/// \atd
/// #define obj_c_category_impl_decl_tuple obj_c_impl_decl_tuple * obj_c_category_impl_decl_info
/// type obj_c_category_impl_decl_info = {
///   ?class_interface : decl_ref option;
///   ?category_decl : decl_ref option;
/// } <ocaml field_prefix="ocidi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D) {
  ASTExporter<ATDWriter>::VisitObjCImplDecl(D);
  ObjectScope Scope(OF);
  const ObjCInterfaceDecl *CI = D->getClassInterface();
  if (CI) {
    OF.emitTag("class_interface");
    dumpBareDeclRef(*CI);
  }
  const ObjCCategoryDecl *CD = D->getCategoryDecl();
  if (CD) {
    OF.emitTag("category_decl");
    dumpBareDeclRef(*CD);
  }
}

/// \atd
/// #define obj_c_protocol_decl_tuple obj_c_container_decl_tuple * obj_c_protocol_decl_info
/// type obj_c_protocol_decl_info = {
///   ~protocols : decl_ref list;
/// } <ocaml field_prefix="opcdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
  ASTExporter<ATDWriter>::VisitObjCContainerDecl(D);
  ObjectScope Scope(OF);
  ObjCCategoryDecl::protocol_iterator I = D->protocol_begin(),
  E = D->protocol_end();
  if (I != E) {
    OF.emitTag("protocols");
    ArrayScope Scope(OF);
    for (; I != E; ++I) {
      assert(*I);
      dumpBareDeclRef(**I);
    }
  }
}

/// \atd
/// #define obj_c_interface_decl_tuple obj_c_container_decl_tuple * obj_c_interface_decl_info
/// type obj_c_interface_decl_info = {
///   ?super : decl_ref option;
///   ?implementation : decl_ref option;
///   ~protocols : decl_ref list;
/// } <ocaml field_prefix="otdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
  VisitObjCContainerDecl(D);
  ObjectScope Scope(OF);
  const ObjCInterfaceDecl *SC = D->getSuperClass();
  if (SC) {
    OF.emitTag("super");
    dumpBareDeclRef(*SC);
  }
  const ObjCImplementationDecl *Impl = D->getImplementation();
  if (Impl) {
    OF.emitTag("implementation");
    dumpBareDeclRef(*Impl);
  }
  ObjCInterfaceDecl::protocol_iterator I = D->protocol_begin(),
  E = D->protocol_end();
  if (I != E) {
    OF.emitTag("protocols");
    ArrayScope Scope(OF);
    for (; I != E; ++I) {
      assert(*I);
      dumpBareDeclRef(**I);
    }
  }
}

/// \atd
/// #define obj_c_implementation_decl_tuple obj_c_impl_decl_tuple * obj_c_implementation_decl_info
/// type obj_c_implementation_decl_info = {
///   ?super : decl_ref option;
///   ?class_interface : decl_ref option;
///   ~ivar_initializers : cxx_ctor_initializer list;
/// } <ocaml field_prefix="oidi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCImplementationDecl(const ObjCImplementationDecl *D) {
  ASTExporter<ATDWriter>::VisitObjCImplDecl(D);
  ObjectScope Scope(OF);
  const ObjCInterfaceDecl *SC = D->getSuperClass();
  if (SC) {
    OF.emitTag("super");
    dumpBareDeclRef(*SC);
  }
  const ObjCInterfaceDecl *CI = D->getClassInterface();
  if (CI) {
    OF.emitTag("class_interface");
    dumpBareDeclRef(*CI);
  }
  ObjCImplementationDecl::init_const_iterator I = D->init_begin(),
  E = D->init_end();
  if (I != E) {
    OF.emitTag("ivar_initializers");
    ArrayScope Scope(OF);
    for (; I != E; ++I) {
      assert(*I);
      dumpBareCXXCtorInitializer(**I);
    }
  }
}

/// \atd
/// #define obj_c_compatible_alias_decl_tuple named_decl_tuple * obj_c_compatible_alias_decl_info
/// type obj_c_compatible_alias_decl_info = {
///   ?class_interface : decl_ref option;
/// } <ocaml field_prefix="ocadi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCCompatibleAliasDecl(const ObjCCompatibleAliasDecl *D) {
  VisitNamedDecl(D);
  ObjectScope Scope(OF);
  const ObjCInterfaceDecl *CI = D->getClassInterface();
  if (CI) {
    OF.emitTag("class_interface");
    dumpBareDeclRef(*CI);
  }
}

/// \atd
/// #define obj_c_property_decl_tuple named_decl_tuple * obj_c_property_decl_info
/// type obj_c_property_decl_info = {
///   ?class_interface : decl_ref option;
///   inherit _qual_type;
///   ~property_control <ocaml default="`None"> : obj_c_property_control;
///   ~property_attributes : property_attribute list
/// } <ocaml field_prefix="opdi_">
/// type obj_c_property_control = [ None | Required | Optional ]
/// type property_attribute = [
///   Readonly
/// | Assign
/// | Readwrite
/// | Retain
/// | Copy
/// | Nonatomic
/// | Atomic
/// | Weak
/// | Strong
/// | Unsafe_unretained
/// | Getter of decl_ref
/// | Setter of decl_ref
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
  VisitNamedDecl(D);
  ObjectScope Scope(OF);
  dumpQualType(D->getType());

  ObjCPropertyDecl::PropertyControl PC = D->getPropertyImplementation();
  if (PC != ObjCPropertyDecl::None) {
    OF.emitTag("property_control");
    switch (PC) {
      case ObjCPropertyDecl::Required: OF.emitSimpleVariant("Required"); break;
      case ObjCPropertyDecl::Optional: OF.emitSimpleVariant("Optional"); break;
      default:
        llvm_unreachable("unknown case");
        break;
    }
  }

  ObjCPropertyDecl::PropertyAttributeKind Attrs = D->getPropertyAttributes();
  if (Attrs != ObjCPropertyDecl::OBJC_PR_noattr) {
    OF.emitTag("property_attributes");
    ArrayScope Scope(OF);
    if (Attrs & ObjCPropertyDecl::OBJC_PR_readonly)
      OF.emitSimpleVariant("Readonly");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_assign)
      OF.emitSimpleVariant("Assign");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_readwrite)
      OF.emitSimpleVariant("Readwrite");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_retain)
      OF.emitSimpleVariant("Retain");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_copy)
      OF.emitSimpleVariant("Copy");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_nonatomic)
      OF.emitSimpleVariant("Nonatomic");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_atomic)
      OF.emitSimpleVariant("Atomic");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_weak)
      OF.emitSimpleVariant("Weak");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_strong)
      OF.emitSimpleVariant("Strong");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_unsafe_unretained)
      OF.emitSimpleVariant("Unsafe_unretained");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_getter) {
      VariantScope Scope(OF, "Getter");
      dumpBareDeclRef(*D->getGetterMethodDecl());
    }
    if (Attrs & ObjCPropertyDecl::OBJC_PR_setter) {
      VariantScope Scope(OF, "Setter");
      dumpBareDeclRef(*D->getSetterMethodDecl());
    }
  }
}

/// \atd
/// #define obj_c_property_impl_decl_tuple decl_tuple * obj_c_property_impl_decl_info
/// type obj_c_property_impl_decl_info = {
///   implementation : property_implementation;
///   ?property_decl : decl_ref option;
///   ?ivar_decl : decl_ref option;
/// } <ocaml field_prefix="opidi_">
/// type property_implementation = [ Synthesize | Dynamic ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D) {
  VisitDecl(D);
  ObjectScope Scope(OF);
  OF.emitTag("implementation");
  switch (D->getPropertyImplementation()) {
    case ObjCPropertyImplDecl::Synthesize: OF.emitSimpleVariant("Synthesize"); break;
    case ObjCPropertyImplDecl::Dynamic: OF.emitSimpleVariant("Dynamic"); break;
  }
  const ObjCPropertyDecl *PD = D->getPropertyDecl();
  if (PD) {
    OF.emitTag("property_decl");
    dumpBareDeclRef(*PD);
  }
  const ObjCIvarDecl *ID = D->getPropertyIvarDecl();
  if (ID) {
    OF.emitTag("ivar_decl");
    dumpBareDeclRef(*ID);
  }
}

/// \atd
/// #define block_decl_tuple decl_tuple * decl_context_tuple * block_decl_info
/// type block_decl_info = {
///   ~parameters : decl list;
///   ~is_variadic : bool;
///   ~captures_cxx_this : bool;
///   ~captured_variables : block_captured_variable list;
///   ?body : stmt option;
/// } <ocaml field_prefix="bdi_">
///
/// type block_captured_variable = {
///    ~is_by_ref : bool;
///    ~is_nested : bool;
///    ?variable : decl_ref option;
///    ?copy_expr : stmt option
/// } <ocaml field_prefix="bcv_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBlockDecl(const BlockDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
  ObjectScope Scope(OF);
  {
    ObjCMethodDecl::param_const_iterator I = D->param_begin(), E = D->param_end();
    if (I != E) {
      OF.emitTag("parameters");
      ArrayScope Scope(OF);
      for (; I != E; ++I) {
        dumpBareDecl(*I);
      }
    }
  }
  OF.emitFlag("is_variadic", D->isVariadic());
  OF.emitFlag("captures_cxx_this", D->capturesCXXThis());
  {
    BlockDecl::capture_iterator I = D->capture_begin(), E = D->capture_end();
    if (I != E) {
      OF.emitTag("captured_variables");
      ArrayScope Scope(OF);
      for (; I != E; ++I) {
        ObjectScope Scope(OF);
        OF.emitFlag("is_by_ref", I->isByRef());
        OF.emitFlag("is_nested", I->isNested());
        if (I->getVariable()) {
          OF.emitTag("variable");
          dumpBareDeclRef(*I->getVariable());
        }
        if (I->hasCopyExpr()) {
          OF.emitTag("copy_expr");
          dumpBareStmt(I->getCopyExpr());
        }
      }
    }
  }
  const Stmt *Body = D->getBody();
  if (Body) {
    OF.emitTag("body");
    dumpBareStmt(Body);
  }
}

// main variant for declarations
/// \atd
/// type decl = [
#define DECL(DERIVED, BASE)   ///   | DERIVED@@Decl of (@DERIVED@_decl_tuple)
#define ABSTRACT_DECL(DECL)
#include <clang/AST/DeclNodes.inc>
/// ] <ocaml repr="classic">

//===----------------------------------------------------------------------===//
//  Stmt dumping methods.
//===----------------------------------------------------------------------===//

// Default aliases for generating variant components
// The main variant is defined at the end of section.
/// \atd
#define STMT(CLASS, PARENT) ///   #define @CLASS@_tuple @PARENT@_tuple
#define ABSTRACT_STMT(STMT) STMT
#include <clang/AST/StmtNodes.inc>
//
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareStmt(const Stmt *S) {
  if (!S) {
    // We use a fixed NullStmt node to represent null pointers
    S = NullPtrStmt;
  }
  VariantScope Scope(OF, S->getStmtClassName());
  {
    TupleScope Scope(OF);
    ConstStmtVisitor<ASTExporter<ATDWriter>>::Visit(S);
  }
}

/// \atd
/// #define stmt_tuple stmt_info * stmt list
/// type stmt_info = {
///   pointer : pointer;
///   source_range : source_range;
/// } <ocaml field_prefix="si_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitStmt(const Stmt *S) {
  {
    ObjectScope Scope(OF);

    OF.emitTag("pointer");
    dumpBarePointer(S);
    OF.emitTag("source_range");
    dumpBareSourceRange(S->getSourceRange());
  }
  {
    ArrayScope Scope(OF);
    for (Stmt::const_child_range CI = S->children(); CI; ++CI) {
      dumpBareStmt(*CI);
    }
  }
}

/// \atd
/// #define decl_stmt_tuple stmt_tuple * decl list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDeclStmt(const DeclStmt *Node) {
  VisitStmt(Node);
  ArrayScope Scope(OF);
  for (DeclStmt::const_decl_iterator I = Node->decl_begin(),
                                     E = Node->decl_end();
       I != E; ++I) {
    dumpBareDecl(*I);
  }
}

/// \atd
/// #define attributed_stmt_tuple stmt_tuple * attribute list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAttributedStmt(const AttributedStmt *Node) {
  VisitStmt(Node);
  ArrayScope Scope(OF);
  for (ArrayRef<const Attr *>::iterator I = Node->getAttrs().begin(),
                                        E = Node->getAttrs().end();
       I != E; ++I) {
    assert(*I);
    dumpBareAttr(**I);
  }
}

/// \atd
/// #define label_stmt_tuple stmt_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitLabelStmt(const LabelStmt *Node) {
  VisitStmt(Node);
  OF.emitString(Node->getName());
}

/// \atd
/// #define goto_stmt_tuple stmt_tuple * goto_stmt_info
/// type goto_stmt_info = {
///   label : string;
///   pointer : pointer
/// } <ocaml field_prefix="gsi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitGotoStmt(const GotoStmt *Node) {
  VisitStmt(Node);
  ObjectScope Scope(OF);
  OF.emitTag("label");
  OF.emitString(Node->getLabel()->getName());
  OF.emitTag("pointer");
  dumpBarePointer(Node->getLabel());
}

/// \atd
/// #define cxx_catch_stmt_tuple stmt_tuple * cxx_catch_stmt_info
/// type cxx_catch_stmt_info = {
///   ?variable : decl option
/// } <ocaml field_prefix="xcsi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXCatchStmt(const CXXCatchStmt *Node) {
  VisitStmt(Node);
  ObjectScope Scope(OF);
  const VarDecl *decl = Node->getExceptionDecl();
  if (decl) {
    OF.emitTag("variable");
    dumpBareDecl(decl);
  }
}

////===----------------------------------------------------------------------===//
////  Expr dumping methods.
////===----------------------------------------------------------------------===//
//

/// \atd
/// #define expr_tuple stmt_tuple * expr_info
/// type expr_info = {
///   inherit _qual_type;
///   ~value_kind <ocaml default="`RValue"> : value_kind;
///   ~object_kind <ocaml default="`Ordinary"> : object_kind;
/// } <ocaml field_prefix="ei_">
///
/// type value_kind = [ RValue | LValue | XValue ]
/// type object_kind = [ Ordinary | BitField | ObjCProperty | ObjCSubscript | VectorComponent ]
///
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExpr(const Expr *Node) {
  VisitStmt(Node);
  ObjectScope Scope(OF);
  dumpQualType(Node->getType());

  ExprValueKind VK = Node->getValueKind();
  if (VK != VK_RValue) {
    OF.emitTag("value_kind");
    switch (VK) {
    case VK_LValue:
      OF.emitSimpleVariant("LValue");
      break;
    case VK_XValue:
      OF.emitSimpleVariant("XValue");
      break;
    default:
      llvm_unreachable("unknown case");
      break;
    }
  }
  ExprObjectKind OK = Node->getObjectKind();
  if (OK != OK_Ordinary) {
    OF.emitTag("object_kind");
    switch (Node->getObjectKind()) {
    case OK_BitField:
      OF.emitSimpleVariant("BitField");
      break;
    case OK_ObjCProperty:
      OF.emitSimpleVariant("ObjCProperty");
      break;
    case OK_ObjCSubscript:
      OF.emitSimpleVariant("ObjCSubscript");
      break;
    case OK_VectorComponent:
      OF.emitSimpleVariant("VectorComponent");
      break;
    default:
      llvm_unreachable("unknown case");
      break;
    }
  }
}

/// \atd
/// type cxx_base_specifier = {
///   name : string;
///   ~virtual : bool;
/// } <ocaml field_prefix="xbs_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareCXXBaseSpecifier(const CXXBaseSpecifier &Base) {
  ObjectScope Scope(OF);
  OF.emitTag("name");
  const CXXRecordDecl *RD = cast<CXXRecordDecl>(Base.getType()->getAs<RecordType>()->getDecl());
  OF.emitString(RD->getName());
  OF.emitFlag("virtual", Base.isVirtual());
}

/// \atd
/// type cast_kind = [
/// | Dependent
/// | BitCast
/// | LValueBitCast
/// | LValueToRValue
/// | NoOp
/// | BaseToDerived
/// | DerivedToBase
/// | UncheckedDerivedToBase
/// | Dynamic
/// | ToUnion
/// | ArrayToPointerDecay
/// | FunctionToPointerDecay
/// | NullToPointer
/// | NullToMemberPointer
/// | BaseToDerivedMemberPointer
/// | DerivedToBaseMemberPointer
/// | MemberPointerToBoolean
/// | ReinterpretMemberPointer
/// | UserDefinedConversion
/// | ConstructorConversion
/// | IntegralToPointer
/// | PointerToIntegral
/// | PointerToBoolean
/// | ToVoid
/// | VectorSplat
/// | IntegralCast
/// | IntegralToBoolean
/// | IntegralToFloating
/// | FloatingToIntegral
/// | FloatingToBoolean
/// | FloatingCast
/// | CPointerToObjCPointerCast
/// | BlockPointerToObjCPointerCast
/// | AnyPointerToBlockPointerCast
/// | ObjCObjectLValueCast
/// | FloatingRealToComplex
/// | FloatingComplexToReal
/// | FloatingComplexToBoolean
/// | FloatingComplexCast
/// | FloatingComplexToIntegralComplex
/// | IntegralRealToComplex
/// | IntegralComplexToReal
/// | IntegralComplexToBoolean
/// | IntegralComplexCast
/// | IntegralComplexToFloatingComplex
/// | ARCProduceObject
/// | ARCConsumeObject
/// | ARCReclaimReturnedObject
/// | ARCExtendBlockObject
/// | AtomicToNonAtomic
/// | NonAtomicToAtomic
/// | CopyAndAutoreleaseBlockObject
/// | BuiltinFnToFnPtr
/// | ZeroToOCLEvent
/// ]
///
/// #define cast_expr_tuple expr_tuple * cast_expr_info
/// type cast_expr_info = {
///   cast_kind : cast_kind;
///   base_path : cxx_base_specifier list;
/// } <ocaml field_prefix="cei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCastExpr(const CastExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("cast_kind");
  OF.emitSimpleVariant(Node->getCastKindName());
  OF.emitTag("base_path");
  {
    ArrayScope Scope(OF);
    for (CastExpr::path_const_iterator I = Node->path_begin(),
         E = Node->path_end();
         I != E; ++I) {
      dumpBareCXXBaseSpecifier(**I);
    }
  }
}
/// \atd
/// #define explicit_cast_expr_tuple cast_expr_tuple * qual_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExplicitCastExpr(const ExplicitCastExpr *Node) {
  VisitCastExpr(Node);
  dumpBareQualType(Node->getTypeAsWritten());
}

/// \atd
/// #define decl_ref_expr_tuple expr_tuple * decl_ref_expr_info
/// type decl_ref_expr_info = {
///   ?decl_ref : decl_ref option;
///   ?found_decl_ref : decl_ref option
/// } <ocaml field_prefix="drti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDeclRefExpr(const DeclRefExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  const ValueDecl *D = Node->getDecl();
  if (D) {
    OF.emitTag("decl_ref");
    dumpBareDeclRef(*D);
  }
  const NamedDecl *FD = Node->getFoundDecl();
  if (FD && D != FD) {
    OF.emitTag("found_decl_ref");
    dumpBareDeclRef(*FD);
  }
}

//template <class ATDWriter>
//void ASTExporter<ATDWriter>::VisitUnresolvedLookupExpr(const UnresolvedLookupExpr *Node) {
//  VisitExpr(Node);
//  OS << " (";
//  if (!Node->requiresADL())
//    OS << "no ";
//  OS << "ADL) = '" << Node->getName() << '\'';
//
//  UnresolvedLookupExpr::decls_iterator
//    I = Node->decls_begin(), E = Node->decls_end();
//  if (I == E)
//    OS << " empty";
//  for (; I != E; ++I)
//    dumpBarePointer(*I);
//}

/// \atd
/// #define obj_c_ivar_ref_expr_tuple expr_tuple * obj_c_ivar_ref_expr_info
/// type obj_c_ivar_ref_expr_info = {
///   decl_ref : decl_ref;
///   pointer : pointer;
///   ~is_free_ivar : bool
/// } <ocaml field_prefix="ovrei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("decl_ref");
  dumpBareDeclRef(*Node->getDecl());
  OF.emitTag("pointer");
  dumpBarePointer(Node->getDecl());
  OF.emitFlag("is_free_ivar", Node->isFreeIvar());
}

/// \atd
/// #define predefined_expr_tuple expr_tuple * predefined_expr_type
/// type predefined_expr_type = [
/// | Func
/// | Function
/// | FuncDName
/// | LFunction
/// | PrettyFunction
/// | PrettyFunctionNoVirtual
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitPredefinedExpr(const PredefinedExpr *Node) {
  VisitExpr(Node);
  switch (Node->getIdentType()) {
  case PredefinedExpr::Func: OF.emitSimpleVariant("Func"); break;
  case PredefinedExpr::Function: OF.emitSimpleVariant("Function"); break;
  case PredefinedExpr::FuncDName: OF.emitSimpleVariant("FuncDName"); break;
  case PredefinedExpr::LFunction: OF.emitSimpleVariant("LFunction"); break;
  case PredefinedExpr::PrettyFunction: OF.emitSimpleVariant("PrettyFunction"); break;
  case PredefinedExpr::PrettyFunctionNoVirtual: OF.emitSimpleVariant("PrettyFunctionNoVirtual"); break;
  }
}

/// \atd
/// #define character_literal_tuple expr_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCharacterLiteral(const CharacterLiteral *Node) {
  VisitExpr(Node);
  OF.emitInteger(Node->getValue());
}

/// \atd
/// #define integer_literal_tuple expr_tuple * integer_literal_info
/// type integer_literal_info = {
///   ~is_signed : bool;
///   bitwidth : int;
///   value : string;
/// } <ocaml field_prefix="ili_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitIntegerLiteral(const IntegerLiteral *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  bool isSigned = Node->getType()->isSignedIntegerType();
  OF.emitFlag("is_signed", isSigned);
  OF.emitTag("bitwidth");
  OF.emitInteger(Node->getValue().getBitWidth());
  OF.emitTag("value");
  OF.emitString(Node->getValue().toString(10, isSigned));
}

/// \atd
/// #define floating_literal_tuple expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFloatingLiteral(const FloatingLiteral *Node) {
  VisitExpr(Node);
  llvm::SmallString<20> buf;
  Node->getValue().toString(buf);
  OF.emitString(buf.str());
}

/// \atd
/// #define string_literal_tuple expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitStringLiteral(const StringLiteral *Str) {
  VisitExpr(Str);
  OF.emitString(Str->getBytes());
}

/// \atd
/// #define unary_operator_tuple expr_tuple * unary_operator_info
/// type unary_operator_info = {
///   kind : unary_operator_kind;
///   ~is_postfix : bool;
/// } <ocaml field_prefix="uoi_">
/// type unary_operator_kind = [
///   PostInc
/// | PostDec
/// | PreInc
/// | PreDec
/// | AddrOf
/// | Deref
/// | Plus
/// | Minus
/// | Not
/// | LNot
/// | Real
/// | Imag
/// | Extension
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitUnaryOperator(const UnaryOperator *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("kind");
  switch (Node->getOpcode()) {
    case UO_PostInc: OF.emitSimpleVariant("PostInc"); break;
    case UO_PostDec: OF.emitSimpleVariant("PostDec"); break;
    case UO_PreInc: OF.emitSimpleVariant("PreInc"); break;
    case UO_PreDec: OF.emitSimpleVariant("PreDec"); break;
    case UO_AddrOf: OF.emitSimpleVariant("AddrOf"); break;
    case UO_Deref: OF.emitSimpleVariant("Deref"); break;
    case UO_Plus: OF.emitSimpleVariant("Plus"); break;
    case UO_Minus: OF.emitSimpleVariant("Minus"); break;
    case UO_Not: OF.emitSimpleVariant("Not"); break;
    case UO_LNot: OF.emitSimpleVariant("LNot"); break;
    case UO_Real: OF.emitSimpleVariant("Real"); break;
    case UO_Imag: OF.emitSimpleVariant("Imag"); break;
    case UO_Extension: OF.emitSimpleVariant("Extension"); break;
  }
  OF.emitFlag("is_postfix", Node->isPostfix());
}

/// \atd
/// #define unary_expr_or_type_trait_expr_tuple expr_tuple * unary_expr_or_type_trait_expr_info
/// type unary_expr_or_type_trait_expr_info = {
///   kind : unary_expr_or_type_trait_kind;
///   ?qual_type : qual_type option
/// } <ocaml field_prefix="uttei_">
///
/// type unary_expr_or_type_trait_kind = [ SizeOf | AlignOf | VecStep ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("kind");
  switch(Node->getKind()) {
  case UETT_SizeOf:
    OF.emitSimpleVariant("SizeOf");
    break;
  case UETT_AlignOf:
    OF.emitSimpleVariant("AlignOf");
    break;
  case UETT_VecStep:
    OF.emitSimpleVariant("VecStep");
    break;
  }
  if (Node->isArgumentType()) {
    dumpQualType(Node->getArgumentType());
  }
}

/// \atd
/// #define member_expr_tuple expr_tuple * member_expr_info
/// type member_expr_info = {
///   ~is_arrow : bool;
///   name : string;
///   pointer : pointer
/// } <ocaml field_prefix="mei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitMemberExpr(const MemberExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitFlag("is_arrow", Node->isArrow());
  OF.emitTag("name");
  OF.emitString(Node->getMemberDecl()->getNameAsString());
  OF.emitTag("pointer");
  dumpBarePointer(Node->getMemberDecl());
}

/// \atd
/// #define ext_vector_element_tuple expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExtVectorElementExpr(const ExtVectorElementExpr *Node) {
  VisitExpr(Node);
  OF.emitString(Node->getAccessor().getNameStart());
}

/// \atd
/// #define binary_operator_tuple expr_tuple * binary_operator_info
/// type binary_operator_info = {
///   kind : binary_operator_kind
/// } <ocaml field_prefix="boi_">
///
/// type binary_operator_kind = [
///   PtrMemD |
///   PtrMemI |
///   Mul |
///   Div |
///   Rem |
///   Add |
///   Sub |
///   Shl |
///   Shr |
///   LT |
///   GT |
///   LE |
///   GE |
///   EQ |
///   NE |
///   And |
///   Xor |
///   Or |
///   LAnd |
///   LOr |
///   Assign |
///   MulAssign |
///   DivAssign |
///   RemAssign |
///   AddAssign |
///   SubAssign |
///   ShlAssign |
///   ShrAssign |
///   AndAssign |
///   XorAssign |
///   OrAssign |
///   Comma
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBinaryOperator(const BinaryOperator *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("kind");
  switch (Node->getOpcode()) {
      case BO_PtrMemD: OF.emitSimpleVariant("PtrMemD"); break;
      case BO_PtrMemI: OF.emitSimpleVariant("PtrMemI"); break;
      case BO_Mul: OF.emitSimpleVariant("Mul"); break;
      case BO_Div: OF.emitSimpleVariant("Div"); break;
      case BO_Rem: OF.emitSimpleVariant("Rem"); break;
      case BO_Add: OF.emitSimpleVariant("Add"); break;
      case BO_Sub: OF.emitSimpleVariant("Sub"); break;
      case BO_Shl: OF.emitSimpleVariant("Shl"); break;
      case BO_Shr: OF.emitSimpleVariant("Shr"); break;
      case BO_LT: OF.emitSimpleVariant("LT"); break;
      case BO_GT: OF.emitSimpleVariant("GT"); break;
      case BO_LE: OF.emitSimpleVariant("LE"); break;
      case BO_GE: OF.emitSimpleVariant("GE"); break;
      case BO_EQ: OF.emitSimpleVariant("EQ"); break;
      case BO_NE: OF.emitSimpleVariant("NE"); break;
      case BO_And: OF.emitSimpleVariant("And"); break;
      case BO_Xor: OF.emitSimpleVariant("Xor"); break;
      case BO_Or: OF.emitSimpleVariant("Or"); break;
      case BO_LAnd: OF.emitSimpleVariant("LAnd"); break;
      case BO_LOr: OF.emitSimpleVariant("LOr"); break;
      case BO_Assign: OF.emitSimpleVariant("Assign"); break;
      case BO_MulAssign: OF.emitSimpleVariant("MulAssign"); break;
      case BO_DivAssign: OF.emitSimpleVariant("DivAssign"); break;
      case BO_RemAssign: OF.emitSimpleVariant("RemAssign"); break;
      case BO_AddAssign: OF.emitSimpleVariant("AddAssign"); break;
      case BO_SubAssign: OF.emitSimpleVariant("SubAssign"); break;
      case BO_ShlAssign: OF.emitSimpleVariant("ShlAssign"); break;
      case BO_ShrAssign: OF.emitSimpleVariant("ShrAssign"); break;
      case BO_AndAssign: OF.emitSimpleVariant("AndAssign"); break;
      case BO_XorAssign: OF.emitSimpleVariant("XorAssign"); break;
      case BO_OrAssign: OF.emitSimpleVariant("OrAssign"); break;
      case BO_Comma: OF.emitSimpleVariant("Comma"); break;
  }
}

/// \atd
/// #define compound_assign_operator_tuple binary_operator_tuple * compound_assign_operator_info
/// type compound_assign_operator_info = {
///   lhs_type : qual_type;
///   result_type : qual_type;
/// } <ocaml field_prefix="caoi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCompoundAssignOperator(const CompoundAssignOperator *Node) {
  VisitBinaryOperator(Node);
  ObjectScope Scope(OF);
  OF.emitTag("lhs_type");
  dumpBareQualType(Node->getComputationLHSType());
  OF.emitTag("result_type");
  dumpBareQualType(Node->getComputationResultType());
}

/// \atd
/// #define block_expr_tuple expr_tuple * decl
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBlockExpr(const BlockExpr *Node) {
  VisitExpr(Node);
  dumpBareDecl(Node->getBlockDecl());
}

/// \atd
/// #define opaque_value_expr_tuple expr_tuple * opaque_value_expr_info
/// type  opaque_value_expr_info = {
///   ?source_expr : stmt option;
/// } <ocaml field_prefix="ovei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitOpaqueValueExpr(const OpaqueValueExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  if (const Expr *Source = Node->getSourceExpr()) {
    OF.emitTag("source_expr");
    dumpBareStmt(Source);
  }
}

// GNU extensions.

/// \atd
/// #define addr_label_expr_tuple expr_tuple * addr_label_expr_info
/// type addr_label_expr_info = {
///   label : string;
///   pointer : pointer;
/// } <ocaml field_prefix="alei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAddrLabelExpr(const AddrLabelExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("label");
  OF.emitString(Node->getLabel()->getName());
  OF.emitTag("pointer");
  dumpBarePointer(Node->getLabel());
}

////===----------------------------------------------------------------------===//
//// C++ Expressions
////===----------------------------------------------------------------------===//

/// \atd
/// #define cxx_named_cast_expr_tuple explicit_cast_expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXNamedCastExpr(const CXXNamedCastExpr *Node) {
  VisitExplicitCastExpr(Node);
  OF.emitString(Node->getCastName());
}

/// \atd
/// #define cxx_bool_literal_expr_tuple expr_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *Node) {
  VisitExpr(Node);
  OF.emitInteger(Node->getValue());
}

/// \atd
/// #define cxx_construct_expr_tuple expr_tuple * cxx_construct_expr_info
/// type cxx_construct_expr_info = {
///   qual_type : qual_type;
///   ~is_elidable : bool;
///   ~requires_zero_initialization : bool;
/// } <ocaml field_prefix="xcei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXConstructExpr(const CXXConstructExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("qual_type");
  CXXConstructorDecl *Ctor = Node->getConstructor();
  dumpBareQualType(Ctor->getType());
  OF.emitFlag("is_elidable", Node->isElidable());
  OF.emitFlag("requires_zero_initialization", Node->requiresZeroInitialization());
}

/// \atd
/// #define cxx_bind_temporary_expr_tuple expr_tuple * cxx_bind_temporary_expr_info
/// type cxx_bind_temporary_expr_info = {
///   cxx_temporary : cxx_temporary;
///   sub_expr : stmt;
/// } <ocaml field_prefix="xbtei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("cxx_temporary");
  dumpBareCXXTemporary(Node->getTemporary());
  OF.emitTag("sub_expr");
  dumpBareStmt(Node->getSubExpr());
}

/// \atd
/// #define materialize_temporary_expr_tuple expr_tuple * materialize_temporary_expr_info
/// type materialize_temporary_expr_info = {
///   ?decl_ref : decl_ref option;
/// } <ocaml field_prefix="mtei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  if (const ValueDecl *VD = Node->getExtendingDecl()) {
    OF.emitTag("decl_ref");
    dumpBareDeclRef(*VD);
  }
}

/// \atd
/// #define expr_with_cleanups_tuple expr_tuple * expr_with_cleanups_info
/// type expr_with_cleanups_info = {
///  ~decl_refs : decl_ref list;
///  sub_expr : stmt;
/// }
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExprWithCleanups(const ExprWithCleanups *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
    if (Node->getNumObjects() > 0) {
      OF.emitTag("decl_refs");
      ArrayScope Scope(OF);
      for (unsigned i = 0, e = Node->getNumObjects(); i != e; ++i)
        dumpBareDeclRef(*Node->getObject(i));
    }
  OF.emitTag("sub_expr");
  dumpBareStmt(Node->getSubExpr());
}

/// \atd
/// type cxx_temporary = pointer
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareCXXTemporary(const CXXTemporary *Temporary) {
  dumpBarePointer(Temporary);
}

////===----------------------------------------------------------------------===//
//// Obj-C Expressions
////===----------------------------------------------------------------------===//

/// \atd
/// #define obj_c_message_expr_tuple expr_tuple * obj_c_message_expr_info
/// type obj_c_message_expr_info = {
///   selector : string;
///   ~receiver_kind <ocaml default="`Instance"> : receiver_kind
/// } <ocaml field_prefix="omei_">
///
/// type receiver_kind = [ Instance | Class of qual_type | SuperInstance | SuperClass ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCMessageExpr(const ObjCMessageExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);

  OF.emitTag("selector");
  OF.emitString(Node->getSelector().getAsString());

  ObjCMessageExpr::ReceiverKind RK = Node->getReceiverKind();
  if (RK != ObjCMessageExpr::Instance) {
    OF.emitTag("receiver_kind");
    switch (RK) {
      case ObjCMessageExpr::Class:
        {
          VariantScope Scope(OF, "Class");
          dumpBareQualType(Node->getClassReceiver());
        }
        break;
      case ObjCMessageExpr::SuperInstance:
        OF.emitSimpleVariant("SuperInstance");
        break;
      case ObjCMessageExpr::SuperClass:
        OF.emitSimpleVariant("SuperClass");
        break;
      default:
        llvm_unreachable("unknown case");
        break;
    }
  }
}

/// \atd
/// type selector = string
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareSelector(const Selector sel) {
  OF.emitString(sel.getAsString());
}

/// \atd
/// #define obj_c_boxed_expr_tuple expr_tuple * selector
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCBoxedExpr(const ObjCBoxedExpr *Node) {
  VisitExpr(Node);
  dumpBareSelector(Node->getBoxingMethod()->getSelector());
}

/// \atd
/// #define obj_c_at_catch_stmt_tuple stmt_tuple * obj_c_message_expr_kind
/// type obj_c_message_expr_kind = [
/// | CatchParam of decl
/// | CatchAll
/// ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node) {
  VisitStmt(Node);
  if (const VarDecl *CatchParam = Node->getCatchParamDecl()) {
    VariantScope Scope(OF, "CatchParam");
    dumpBareDecl(CatchParam);
  } else {
    OF.emitSimpleVariant("CatchAll");
  }
}

/// \atd
/// #define obj_c_encode_expr_tuple expr_tuple * qual_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCEncodeExpr(const ObjCEncodeExpr *Node) {
  VisitExpr(Node);
  dumpBareQualType(Node->getEncodedType());
}

/// \atd
/// #define obj_c_selector_expr_tuple expr_tuple * selector
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCSelectorExpr(const ObjCSelectorExpr *Node) {
  VisitExpr(Node);
  dumpBareSelector(Node->getSelector());
}

/// \atd
/// #define obj_c_protocol_expr_tuple expr_tuple * decl_ref
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCProtocolExpr(const ObjCProtocolExpr *Node) {
  VisitExpr(Node);
  dumpBareDeclRef(*Node->getProtocol());
}

/// \atd
/// #define obj_c_property_ref_expr_tuple expr_tuple * obj_c_property_ref_expr_info
///
/// type obj_c_property_ref_expr_info = {
///   kind : property_ref_kind;
///   ~is_super_receiver : bool;
///   ~is_messaging_getter : bool;
///   ~is_messaging_setter : bool;
/// } <ocaml field_prefix="oprei_">
///
/// type property_ref_kind = [
/// | MethodRef of obj_c_method_ref_info
/// | PropertyRef of decl_ref
/// ]
///
/// type obj_c_method_ref_info = {
///   ?getter : selector option;
///   ?setter : selector option
/// } <ocaml field_prefix="mri_">
///
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("kind");
  if (Node->isImplicitProperty()) {
    VariantScope Scope(OF, "MethodRef");
    {
      ObjectScope Scope(OF);
      if (Node->getImplicitPropertyGetter()) {
        OF.emitTag("getter");
        dumpBareSelector(Node->getImplicitPropertyGetter()->getSelector());
      }
      if (Node->getImplicitPropertySetter()) {
        OF.emitTag("setter");
        dumpBareSelector(Node->getImplicitPropertySetter()->getSelector());
      }
    }
  } else {
    VariantScope Scope(OF, "PropertyRef");
    dumpBareDeclRef(*Node->getExplicitProperty());
  }
  OF.emitFlag("is_super_receiver", Node->isSuperReceiver());
  OF.emitFlag("is_messaging_getter", Node->isMessagingGetter());
  OF.emitFlag("is_messaging_setter", Node->isMessagingSetter());
}

/// \atd
/// #define obj_c_subscript_ref_expr_tuple expr_tuple * obj_c_subscript_ref_expr_info
///
/// type obj_c_subscript_ref_expr_info = {
///   kind : obj_c_subscript_kind;
///   ?getter : selector option;
///   ?setter : selector option
/// } <ocaml field_prefix="osrei_">
///
/// type obj_c_subscript_kind = [ ArraySubscript | DictionarySubscript ]
///
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCSubscriptRefExpr(const ObjCSubscriptRefExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF);
  OF.emitTag("kind");
  if (Node->isArraySubscriptRefExpr()) {
    OF.emitSimpleVariant("ArraySubscript");
  } else {
    OF.emitSimpleVariant("DictionarySubscript");
  }
  if (Node->getAtIndexMethodDecl()) {
    OF.emitTag("getter");
    dumpBareSelector(Node->getAtIndexMethodDecl()->getSelector());
  }
  if (Node->setAtIndexMethodDecl()) {
    OF.emitTag("setter");
    dumpBareSelector(Node->setAtIndexMethodDecl()->getSelector());
  }
}

/// \atd
/// #define obj_c_bool_literal_expr_tuple expr_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *Node) {
  VisitExpr(Node);
  OF.emitInteger(Node->getValue());
}


// Main variant for statements
/// \atd
/// type stmt = [
#define STMT(CLASS, PARENT) ///   | CLASS of (@CLASS@_tuple)
#define ABSTRACT_STMT(STMT)
#include <clang/AST/StmtNodes.inc>
/// ] <ocaml repr="classic">

//===----------------------------------------------------------------------===//
// Comments
//===----------------------------------------------------------------------===//

template <class ATDWriter>
const char *ASTExporter<ATDWriter>::getCommandName(unsigned CommandID) {
  return Traits.getCommandInfo(CommandID)->Name;
}

/// \atd
/// type _full_comment = { ?full_comment : comment option }
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpFullComment(const FullComment *C) {
  if (!C)
    return;

  OF.emitTag("full_comment");
  FC = C;
  dumpBareComment(C);
  FC = 0;
}

/// \atd
#define COMMENT(CLASS, PARENT) /// #define @CLASS@_tuple @PARENT@_tuple
#define ABSTRACT_COMMENT(COMMENT) COMMENT
#include <clang/AST/CommentNodes.inc>
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareComment(const Comment *C) {
  if (!C) {
    // We use a fixed NoComment node to represent null pointers
    C = NullPtrComment;
  }
  VariantScope Scope(OF, std::string(C->getCommentKindName()));
  {
    TupleScope Scope(OF);
    ConstCommentVisitor<ASTExporter<ATDWriter>>::visit(C);
  }
}

/// \atd
/// #define comment_tuple comment_info * comment list
/// type comment_info = {
///   parent_pointer : pointer;
///   source_range : source_range;
/// } <ocaml field_prefix="ci_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::visitComment(const Comment *C) {
  {
    ObjectScope ObjComment(OF);
    OF.emitTag("parent_pointer");
    dumpBarePointer(C);
    OF.emitTag("source_range");
    dumpBareSourceRange(C->getSourceRange());
  }
  {
    ArrayScope Scope(OF);
    for (Comment::child_iterator I = C->child_begin(), E = C->child_end();
         I != E; ++I) {
      dumpBareComment(*I);
    }
  }
}

/// \atd
/// #define text_comment_tuple comment_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::visitTextComment(const TextComment *C) {
  visitComment(C);
  OF.emitString(C->getText());
}

//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitInlineCommandComment(const InlineCommandComment *C) {
//  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
//  switch (C->getRenderKind()) {
//  case InlineCommandComment::RenderNormal:
//    OS << " RenderNormal";
//    break;
//  case InlineCommandComment::RenderBold:
//    OS << " RenderBold";
//    break;
//  case InlineCommandComment::RenderMonospaced:
//    OS << " RenderMonospaced";
//    break;
//  case InlineCommandComment::RenderEmphasized:
//    OS << " RenderEmphasized";
//    break;
//  }
//
//  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
//    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitHTMLStartTagComment(const HTMLStartTagComment *C) {
//  OS << " Name=\"" << C->getTagName() << "\"";
//  if (C->getNumAttrs() != 0) {
//    OS << " Attrs: ";
//    for (unsigned i = 0, e = C->getNumAttrs(); i != e; ++i) {
//      const HTMLStartTagComment::Attribute &Attr = C->getAttr(i);
//      OS << " \"" << Attr.Name << "=\"" << Attr.Value << "\"";
//    }
//  }
//  if (C->isSelfClosing())
//    OS << " SelfClosing";
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitHTMLEndTagComment(const HTMLEndTagComment *C) {
//  OS << " Name=\"" << C->getTagName() << "\"";
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitBlockCommandComment(const BlockCommandComment *C) {
//  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
//  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
//    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitParamCommandComment(const ParamCommandComment *C) {
//  OS << " " << ParamCommandComment::getDirectionAsString(C->getDirection());
//
//  if (C->isDirectionExplicit())
//    OS << " explicitly";
//  else
//    OS << " implicitly";
//
//  if (C->hasParamName()) {
//    if (C->isParamIndexValid())
//      OS << " Param=\"" << C->getParamName(FC) << "\"";
//    else
//      OS << " Param=\"" << C->getParamNameAsWritten() << "\"";
//  }
//
//  if (C->isParamIndexValid())
//    OS << " ParamIndex=" << C->getParamIndex();
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitTParamCommandComment(const TParamCommandComment *C) {
//  if (C->hasParamName()) {
//    if (C->isPositionValid())
//      OS << " Param=\"" << C->getParamName(FC) << "\"";
//    else
//      OS << " Param=\"" << C->getParamNameAsWritten() << "\"";
//  }
//
//  if (C->isPositionValid()) {
//    OS << " Position=<";
//    for (unsigned i = 0, e = C->getDepth(); i != e; ++i) {
//      OS << C->getIndex(i);
//      if (i != e - 1)
//        OS << ", ";
//    }
//    OS << ">";
//  }
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitVerbatimBlockComment(const VerbatimBlockComment *C) {
//  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\""
//        " CloseName=\"" << C->getCloseName() << "\"";
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitVerbatimBlockLineComment(
//    const VerbatimBlockLineComment *C) {
//  OS << " Text=\"" << C->getText() << "\"";
//}
//
//template <class ATDWriter>
//void ASTExporter<ATDWriter>::visitVerbatimLineComment(const VerbatimLineComment *C) {
//  OS << " Text=\"" << C->getText() << "\"";
//}

/// \atd
/// type comment = [
#define COMMENT(CLASS, PARENT) ///   | CLASS of (@CLASS@_tuple)
#define ABSTRACT_COMMENT(COMMENT)
#include <clang/AST/CommentNodes.inc>
/// ] <ocaml repr="classic">

//===----------------------------------------------------------------------===//
// ASTExporter Plugin Main
//===----------------------------------------------------------------------===//

namespace {

  template <class ATDWriter>
  class ExporterASTConsumer : public ASTConsumer {
  private:
    std::string BasePath;
    std::string DeduplicationServicePath;
    raw_ostream &OS;

  public:
    ExporterASTConsumer(CompilerInstance &CI,
                        StringRef InputFile,
                        StringRef BasePath,
                        StringRef DeduplicationServicePath,
                        raw_ostream &OS)
    : BasePath(BasePath),
      DeduplicationServicePath(DeduplicationServicePath),
      OS(OS)
    {}

    virtual void HandleTranslationUnit(ASTContext &Context) {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      FileUtils::DeduplicationService Dedup(DeduplicationServicePath, BasePath, Context.getSourceManager());
      ASTExporter<ATDWriter> P(OS, Context, BasePath, DeduplicationServicePath != "" ? &Dedup : nullptr);
      P.dumpBareDecl(D);
    }
  };

}

typedef SimplePluginASTAction<ExporterASTConsumer<JsonWriter>> JsonExporterASTAction;
typedef SimplePluginASTAction<ExporterASTConsumer<YojsonWriter>> YojsonExporterASTAction;

static FrontendPluginRegistry::Add<JsonExporterASTAction>
X("JsonASTExporter", "Export the AST of source files into ATD-specified Json data");

static FrontendPluginRegistry::Add<YojsonExporterASTAction>
Y("YojsonASTExporter", "Export the AST of source files into ATD-specified Yojson data");
