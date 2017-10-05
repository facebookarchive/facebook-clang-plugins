/**
 * Copyright (c) 2014, Facebook, Inc.
 * Copyright (c) 2003-2014 University of Illinois at Urbana-Champaign.
 * All rights reserved.
 *
 * This file is distributed under the University of Illinois Open Source
 * License.
 * See LLVM-LICENSE for details.
 *
 */

/**
 * Utility class to export an AST of clang into Json and Yojson (and ultimately
 * Biniou)
 * while conforming to the inlined ATD specifications.
 *
 * /!\
 * '//@atd' comments are meant to be extracted and processed to generate ATD
 * specifications for the Json dumper.
 * Do not modify ATD comments without modifying the Json emission accordingly
 * (and conversely).
 * See ATD_GUIDELINES.md for more guidelines on how to write and test ATD
 * annotations.
 *
 * This file was obtained by modifying the file ASTdumper.cpp from the
 * LLVM/clang project.
 * The general layout should be maintained to make future merging easier.
 */

#pragma once
#include <memory>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/CommentVisitor.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclLookups.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/DeclVisitor.h>
#include <clang/AST/Mangle.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/AST/TypeVisitor.h>
#include <clang/Basic/Module.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendDiagnostic.h>
#include <clang/Frontend/FrontendPluginRegistry.h>

#include <llvm/Support/raw_ostream.h>

#include "AttrParameterVectorStream.h"
#include "NamePrinter.h"
#include "SimplePluginASTAction.h"
#include "atdlib/ATDWriter.h"

//===----------------------------------------------------------------------===//
// ASTExporter Visitor
//===----------------------------------------------------------------------===//

namespace ASTLib {

struct ASTExporterOptions : ASTPluginLib::PluginASTOptionsBase {
  bool withPointers = true;
  bool dumpComments = false;
  bool useMacroExpansionLocation = true;
  ATDWriter::ATDWriterOptions atdWriterOptions = {
      .useYojson = false, .prettifyJson = true,
  };

  void loadValuesFromEnvAndMap(
      const ASTPluginLib::PluginASTOptionsBase::argmap_t &map) {
    ASTPluginLib::PluginASTOptionsBase::loadValuesFromEnvAndMap(map);
    loadBool(map, "AST_WITH_POINTERS", withPointers);
    loadBool(map, "USE_YOJSON", atdWriterOptions.useYojson);
    loadBool(map, "PRETTIFY_JSON", atdWriterOptions.prettifyJson);
  }
};

using namespace clang;
using namespace clang::comments;

template <class Impl>
struct TupleSizeBase {
// Decls

#define DECL(DERIVED, BASE)                              \
  int DERIVED##DeclTupleSize() {                         \
    return static_cast<Impl *>(this)->BASE##TupleSize(); \
  }
#define ABSTRACT_DECL(DECL) DECL
#include <clang/AST/DeclNodes.inc>

  int tupleSizeOfDeclKind(const Decl::Kind kind) {
    switch (kind) {
#define DECL(DERIVED, BASE) \
  case Decl::DERIVED:       \
    return static_cast<Impl *>(this)->DERIVED##DeclTupleSize();
#define ABSTRACT_DECL(DECL)
#include <clang/AST/DeclNodes.inc>
    }
    llvm_unreachable("Decl that isn't part of DeclNodes.inc!");
  }

// Stmts

#define STMT(CLASS, PARENT)                                \
  int CLASS##TupleSize() {                                 \
    return static_cast<Impl *>(this)->PARENT##TupleSize(); \
  }
#define ABSTRACT_STMT(STMT) STMT
#include <clang/AST/StmtNodes.inc>

  int tupleSizeOfStmtClass(const Stmt::StmtClass stmtClass) {
    switch (stmtClass) {
#define STMT(CLASS, PARENT) \
  case Stmt::CLASS##Class:  \
    return static_cast<Impl *>(this)->CLASS##TupleSize();
#define ABSTRACT_STMT(STMT)
#include <clang/AST/StmtNodes.inc>
    case Stmt::NoStmtClass:
      break;
    }
    llvm_unreachable("Stmt that isn't part of StmtNodes.inc!");
  }

// Comments

#define COMMENT(CLASS, PARENT)                             \
  int CLASS##TupleSize() {                                 \
    return static_cast<Impl *>(this)->PARENT##TupleSize(); \
  }
#define ABSTRACT_COMMENT(COMMENT) COMMENT
#include <clang/AST/CommentNodes.inc>

  int tupleSizeOfCommentKind(const Comment::CommentKind kind) {
    switch (kind) {
#define COMMENT(CLASS, PARENT) \
  case Comment::CLASS##Kind:   \
    return static_cast<Impl *>(this)->CLASS##TupleSize();
#define ABSTRACT_COMMENT(COMMENT)
#include <clang/AST/CommentNodes.inc>
    case Comment::NoCommentKind:
      break;
    }
    llvm_unreachable("Comment that isn't part of CommentNodes.inc!");
  }

// Types

#define TYPE(DERIVED, BASE)                              \
  int DERIVED##TypeTupleSize() {                         \
    return static_cast<Impl *>(this)->BASE##TupleSize(); \
  }
#define ABSTRACT_TYPE(DERIVED, BASE) TYPE(DERIVED, BASE)
#include <clang/AST/TypeNodes.def>

  int tupleSizeOfTypeClass(const Type::TypeClass typeClass) {
    switch (typeClass) {
#define TYPE(DERIVED, BASE) \
  case Type::DERIVED:       \
    return static_cast<Impl *>(this)->DERIVED##TypeTupleSize();
#define ABSTRACT_TYPE(DERIVED, BASE)
#include <clang/AST/TypeNodes.def>
    }
    llvm_unreachable("Type that isn't part of TypeNodes.def!");
  }
};

typedef ATDWriter::JsonWriter<raw_ostream> JsonWriter;

template <class ATDWriter = JsonWriter>
class ASTExporter : public ConstDeclVisitor<ASTExporter<ATDWriter>>,
                    public ConstStmtVisitor<ASTExporter<ATDWriter>>,
                    public ConstCommentVisitor<ASTExporter<ATDWriter>>,
                    public TypeVisitor<ASTExporter<ATDWriter>>,
                    public TupleSizeBase<ASTExporter<ATDWriter>> {
  typedef typename ATDWriter::ObjectScope ObjectScope;
  typedef typename ATDWriter::ArrayScope ArrayScope;
  typedef typename ATDWriter::TupleScope TupleScope;
  typedef typename ATDWriter::VariantScope VariantScope;
  ATDWriter OF;

  ASTContext &Context;

  const ASTExporterOptions &Options;

  std::unique_ptr<MangleContext> Mangler;

  // Encoding of NULL pointers into suitable empty nodes
  // This is a hack but using option types in children lists would make the Json
  // terribly verbose.
  // Also these useless nodes could have occurred in the original AST anyway :)
  //
  // Note: We are not using std::unique_ptr because 'delete' appears to be
  // protected (at least on Stmt).
  const Stmt *const NullPtrStmt;
  const Decl *const NullPtrDecl;
  const Comment *const NullPtrComment;

  // Keep track of the last location we print out so that we can
  // print out deltas from then on out.
  const char *LastLocFilename;
  unsigned LastLocLine;

  // The \c FullComment parent of the comment being dumped.
  const FullComment *FC;

  NamePrinter<ATDWriter> NamePrint;

 public:
  ASTExporter(raw_ostream &OS,
              ASTContext &Context,
              const ASTExporterOptions &Opts)
      : OF(OS, Opts.atdWriterOptions),
        Context(Context),
        Options(Opts),
        Mangler(
            ItaniumMangleContext::create(Context, Context.getDiagnostics())),
        NullPtrStmt(new (Context) NullStmt(SourceLocation())),
        NullPtrDecl(EmptyDecl::Create(
            Context, Context.getTranslationUnitDecl(), SourceLocation())),
        NullPtrComment(new (Context) Comment(
            Comment::NoCommentKind, SourceLocation(), SourceLocation())),
        LastLocFilename(""),
        LastLocLine(~0U),
        FC(0),
        NamePrint(Context.getSourceManager(), OF) {}

  void dumpDecl(const Decl *D);
  void dumpStmt(const Stmt *S);
  void dumpFullComment(const FullComment *C);
  void dumpType(const Type *T);
  void dumpPointerToType(const Type *T);
  void dumpQualTypeNoQuals(const QualType &qt);
  void dumpClassLambdaCapture(const LambdaCapture *C);

  // Utilities
  void dumpPointer(const void *Ptr);
  void dumpSourceRange(SourceRange R);
  void dumpSourceLocation(SourceLocation Loc);
  void dumpQualType(const QualType &qt);
  void dumpTypeOld(const Type *T);
  void dumpDeclRef(const Decl &Node);
  bool hasNodes(const DeclContext *DC);
  void dumpLookups(const DeclContext &DC);
  void dumpAttr(const Attr &A);
  void dumpSelector(const Selector sel);
  void dumpName(const NamedDecl &decl);
  void dumpInputKind(const InputKind kind);

  bool alwaysEmitParent(const Decl *D);

  // C++ Utilities
  void dumpAccessSpecifier(AccessSpecifier AS);
  void dumpCXXCtorInitializer(const CXXCtorInitializer &Init);
  void dumpDeclarationName(const DeclarationName &Name);
  void dumpNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS);
  void dumpTemplateArgument(const TemplateArgument &Arg);
  void dumpTemplateSpecialization(const TemplateDecl *D,
                                  const TemplateArgumentList &Args);
  //    void dumpTemplateParameters(const TemplateParameterList *TPL);
  //    void dumpTemplateArgumentListInfo(const TemplateArgumentListInfo &TALI);
  //    void dumpTemplateArgumentLoc(const TemplateArgumentLoc &A);
  //    void dumpTemplateArgumentList(const TemplateArgumentList &TAL);
  //    void dumpTemplateArgument(const TemplateArgument &A,
  //                              SourceRange R = SourceRange());
  void dumpCXXBaseSpecifier(const CXXBaseSpecifier &Base);

#define DECLARE_VISITOR(NAME) \
  int NAME##TupleSize();      \
  void Visit##NAME(const NAME *D);
#define DECLARE_LOWERCASE_VISITOR(NAME) \
  int NAME##TupleSize();                \
  void visit##NAME(const NAME *D);

  // Decls
  DECLARE_VISITOR(Decl)
  DECLARE_VISITOR(DeclContext)
  DECLARE_VISITOR(BlockDecl)
  DECLARE_VISITOR(CapturedDecl)
  DECLARE_VISITOR(LinkageSpecDecl)
  DECLARE_VISITOR(NamespaceDecl)
  DECLARE_VISITOR(ObjCContainerDecl)
  DECLARE_VISITOR(TagDecl)
  DECLARE_VISITOR(TypeDecl)
  DECLARE_VISITOR(TranslationUnitDecl)
  DECLARE_VISITOR(NamedDecl)
  DECLARE_VISITOR(ValueDecl)
  DECLARE_VISITOR(TypedefDecl)
  DECLARE_VISITOR(EnumDecl)
  DECLARE_VISITOR(RecordDecl)
  DECLARE_VISITOR(EnumConstantDecl)
  DECLARE_VISITOR(IndirectFieldDecl)
  DECLARE_VISITOR(FunctionDecl)
  DECLARE_VISITOR(FieldDecl)
  DECLARE_VISITOR(VarDecl)
  DECLARE_VISITOR(FileScopeAsmDecl)
  DECLARE_VISITOR(ImportDecl)

  // C++ Decls
  DECLARE_VISITOR(UsingDirectiveDecl)
  DECLARE_VISITOR(NamespaceAliasDecl)
  DECLARE_VISITOR(CXXRecordDecl)
  DECLARE_VISITOR(ClassTemplateSpecializationDecl)
  DECLARE_VISITOR(CXXMethodDecl)
  DECLARE_VISITOR(ClassTemplateDecl)
  DECLARE_VISITOR(FunctionTemplateDecl)
  DECLARE_VISITOR(FriendDecl)

  //    void VisitTypeAliasDecl(const TypeAliasDecl *D);
  //    void VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D);
  //    void VisitStaticAssertDecl(const StaticAssertDecl *D);
  //    template<typename SpecializationDecl>
  //    void VisitTemplateDeclSpecialization(ChildDumper &Children,
  //                                         const SpecializationDecl *D,
  //                                         bool DumpExplicitInst,
  //                                         bool DumpRefOnly);
  //    void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D);
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
  //    void VisitUnresolvedUsingTypenameDecl(const UnresolvedUsingTypenameDecl
  //    *D);
  //    void VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D);
  //    void VisitUsingShadowDecl(const UsingShadowDecl *D);
  //    void VisitLinkageSpecDecl(const LinkageSpecDecl *D);
  //    void VisitAccessSpecDecl(const AccessSpecDecl *D);
  //
  //    // ObjC Decls
  DECLARE_VISITOR(ObjCIvarDecl)
  DECLARE_VISITOR(ObjCMethodDecl)
  DECLARE_VISITOR(ObjCCategoryDecl)
  DECLARE_VISITOR(ObjCCategoryImplDecl)
  DECLARE_VISITOR(ObjCProtocolDecl)
  DECLARE_VISITOR(ObjCInterfaceDecl)
  DECLARE_VISITOR(ObjCImplementationDecl)
  DECLARE_VISITOR(ObjCCompatibleAliasDecl)
  DECLARE_VISITOR(ObjCPropertyDecl)
  DECLARE_VISITOR(ObjCPropertyImplDecl)

  // Stmts.
  DECLARE_VISITOR(Stmt)
  DECLARE_VISITOR(DeclStmt)
  DECLARE_VISITOR(AttributedStmt)
  DECLARE_VISITOR(LabelStmt)
  DECLARE_VISITOR(GotoStmt)
  DECLARE_VISITOR(CXXCatchStmt)

  // Exprs
  DECLARE_VISITOR(Expr)
  DECLARE_VISITOR(CastExpr)
  DECLARE_VISITOR(ExplicitCastExpr)
  DECLARE_VISITOR(DeclRefExpr)
  DECLARE_VISITOR(PredefinedExpr)
  DECLARE_VISITOR(CharacterLiteral)
  DECLARE_VISITOR(IntegerLiteral)
  DECLARE_VISITOR(FloatingLiteral)
  DECLARE_VISITOR(StringLiteral)
  //    DECLARE_VISITOR(InitListExpr)
  DECLARE_VISITOR(UnaryOperator)
  DECLARE_VISITOR(UnaryExprOrTypeTraitExpr)
  DECLARE_VISITOR(MemberExpr)
  DECLARE_VISITOR(ExtVectorElementExpr)
  DECLARE_VISITOR(BinaryOperator)
  DECLARE_VISITOR(CompoundAssignOperator)
  DECLARE_VISITOR(AddrLabelExpr)
  DECLARE_VISITOR(BlockExpr)
  DECLARE_VISITOR(OpaqueValueExpr)

  // C++
  DECLARE_VISITOR(CXXNamedCastExpr)
  DECLARE_VISITOR(CXXBoolLiteralExpr)
  DECLARE_VISITOR(CXXConstructExpr)
  DECLARE_VISITOR(CXXBindTemporaryExpr)
  DECLARE_VISITOR(MaterializeTemporaryExpr)
  DECLARE_VISITOR(ExprWithCleanups)
  DECLARE_VISITOR(OverloadExpr)
  DECLARE_VISITOR(UnresolvedLookupExpr)
  void dumpCXXTemporary(const CXXTemporary *Temporary);
  DECLARE_VISITOR(LambdaExpr)
  DECLARE_VISITOR(CXXNewExpr)
  DECLARE_VISITOR(CXXDeleteExpr)
  DECLARE_VISITOR(CXXDefaultArgExpr)
  DECLARE_VISITOR(CXXDefaultInitExpr)
  DECLARE_VISITOR(TypeTraitExpr)
  DECLARE_VISITOR(CXXNoexceptExpr)

  // ObjC
  DECLARE_VISITOR(ObjCAtCatchStmt)
  DECLARE_VISITOR(ObjCEncodeExpr)
  DECLARE_VISITOR(ObjCMessageExpr)
  DECLARE_VISITOR(ObjCBoxedExpr)
  DECLARE_VISITOR(ObjCSelectorExpr)
  DECLARE_VISITOR(ObjCProtocolExpr)
  DECLARE_VISITOR(ObjCPropertyRefExpr)
  DECLARE_VISITOR(ObjCSubscriptRefExpr)
  DECLARE_VISITOR(ObjCIvarRefExpr)
  DECLARE_VISITOR(ObjCBoolLiteralExpr)
  DECLARE_VISITOR(ObjCAvailabilityCheckExpr)

  // Comments.
  const char *getCommandName(unsigned CommandID);
  void dumpComment(const Comment *C);

  // Inline comments.
  DECLARE_LOWERCASE_VISITOR(Comment)
  DECLARE_LOWERCASE_VISITOR(TextComment)
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

  // Types - no template type handling yet
  int TypeWithChildInfoTupleSize();
  DECLARE_VISITOR(Type)
  DECLARE_VISITOR(AdjustedType)
  DECLARE_VISITOR(ArrayType)
  DECLARE_VISITOR(ConstantArrayType)
  //  DECLARE_VISITOR(DependentSizedArrayType)
  //  DECLARE_VISITOR(IncompleteArrayType)
  DECLARE_VISITOR(VariableArrayType)
  DECLARE_VISITOR(AtomicType)
  DECLARE_VISITOR(AttributedType) // getEquivalentType() + getAttrKind -> string
  //  DECLARE_VISITOR(AutoType)
  DECLARE_VISITOR(BlockPointerType)
  DECLARE_VISITOR(BuiltinType)
  //  DECLARE_VISITOR(ComplexType)
  DECLARE_VISITOR(DecltypeType)
  //  DECLARE_VISITOR(DependentSizedExtVectorType)
  DECLARE_VISITOR(FunctionType)
  //  DECLARE_VISITOR(FunctionNoProtoType)
  DECLARE_VISITOR(FunctionProtoType)
  //  DECLARE_VISITOR(InjectedClassNameType)
  DECLARE_VISITOR(MemberPointerType)
  DECLARE_VISITOR(ObjCObjectPointerType)
  DECLARE_VISITOR(ObjCObjectType)
  DECLARE_VISITOR(ObjCInterfaceType)
  DECLARE_VISITOR(ParenType)
  DECLARE_VISITOR(PointerType)
  DECLARE_VISITOR(ReferenceType)
  DECLARE_VISITOR(TagType)
  DECLARE_VISITOR(TypedefType)

  void dumpTypeAttr(AttributedType::Kind kind);
  void dumpObjCLifetimeQual(Qualifiers::ObjCLifetime qual);

  /* #define TYPE(CLASS, PARENT) DECLARE_VISITOR(CLASS##Type) */
  /* #define ABSTRACT_TYPE(CLASS, PARENT) */
  /* #include <clang/AST/TypeNodes.def> */
};

//===----------------------------------------------------------------------===//
//  Utilities
//===----------------------------------------------------------------------===//

bool hasMeaningfulTypeInfo(const Type *T) {
  // clang goes into an infinite loop trying to compute the TypeInfo of
  // dependent types, and a width of 0 if the type doesn't have a constant size
  return T && !T->isIncompleteType() && !T->isDependentType() &&
         T->isConstantSizeType();
}

std::unordered_map<const void *, int> pointerMap;
int pointerCounter = 1;

//@atd type pointer = int
template <class ATDWriter>
void writePointer(ATDWriter &OF, bool withPointers, const void *Ptr) {
  if (!Ptr) {
    OF.emitInteger(0);
    return;
  }
  if (pointerMap.find(Ptr) == pointerMap.end()) {
    pointerMap[Ptr] = pointerCounter++;
  }
  OF.emitInteger(pointerMap[Ptr]);
}

template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpPointer(const void *Ptr) {
  writePointer(OF, Options.withPointers, Ptr);
}

//@atd type source_file = string
//@atd type source_location = {
//@atd   ?file <ocaml mutable> : source_file option;
//@atd   ?line <ocaml mutable> : int option;
//@atd   ?column <ocaml mutable> : int option;
//@atd } <ocaml field_prefix="sl_" validator="Clang_ast_visit.visit_source_loc">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpSourceLocation(SourceLocation Loc) {
  const SourceManager &SM = Context.getSourceManager();
  SourceLocation ExpLoc =
      Options.useMacroExpansionLocation ? SM.getExpansionLoc(Loc) : Loc;
  SourceLocation SpellingLoc = SM.getSpellingLoc(ExpLoc);

  // The general format we print out is filename:line:col, but we drop pieces
  // that haven't changed since the last loc printed.
  PresumedLoc PLoc = SM.getPresumedLoc(SpellingLoc);

  if (PLoc.isInvalid()) {
    ObjectScope Scope(OF, 0);
    return;
  }

  if (strcmp(PLoc.getFilename(), LastLocFilename) != 0) {
    ObjectScope Scope(OF, 3);
    OF.emitTag("file");
    // Normalizing filenames matters because the current directory may change
    // during the compilation of large projects.
    OF.emitString(Options.normalizeSourcePath(PLoc.getFilename()));
    OF.emitTag("line");
    OF.emitInteger(PLoc.getLine());
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  } else if (PLoc.getLine() != LastLocLine) {
    ObjectScope Scope(OF, 2);
    OF.emitTag("line");
    OF.emitInteger(PLoc.getLine());
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  } else {
    ObjectScope Scope(OF, 1);
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  }
  LastLocFilename = PLoc.getFilename();
  LastLocLine = PLoc.getLine();
  // TODO: lastLocColumn
}

//@atd type source_range = (source_location * source_location)
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpSourceRange(SourceRange R) {
  TupleScope Scope(OF, 2);
  dumpSourceLocation(R.getBegin());
  dumpSourceLocation(R.getEnd());
}

//@atd type qual_type = {
//@atd   type_ptr : type_ptr;
//@atd   ~is_const : bool;
//@atd   ~is_restrict : bool;
//@atd   ~is_volatile : bool;
//@atd } <ocaml field_prefix="qt_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpQualType(const QualType &qt) {
  clang::Qualifiers Quals =
      qt.isNull() ? clang::Qualifiers() : qt.getQualifiers();
  bool isConst = Quals.hasConst();
  bool isRestrict = Quals.hasRestrict();
  bool isVolatile = Quals.hasVolatile();
  ObjectScope oScope(OF, 1 + isConst + isVolatile + isRestrict);
  OF.emitTag("type_ptr");
  dumpQualTypeNoQuals(qt);
  OF.emitFlag("is_const", isConst);
  OF.emitFlag("is_restrict", isRestrict);
  OF.emitFlag("is_volatile", isVolatile);
}

//@atd type named_decl_info = {
//@atd   name : string;
//@atd   qual_name : string list;
//@atd } <ocaml field_prefix="ni_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpName(const NamedDecl &Decl) {
  // dump name
  ObjectScope oScope(OF, 2);
  OF.emitTag("name");
  OF.emitString(Decl.getNameAsString());

  OF.emitTag("qual_name");
  NamePrint.printDeclName(Decl);
}

//@atd type decl_ref = {
//@atd   kind : decl_kind;
//@atd   decl_pointer : pointer;
//@atd   ?name : named_decl_info option;
//@atd   ~is_hidden : bool;
//@atd   ?qual_type : qual_type option
//@atd } <ocaml field_prefix="dr_">
//@atd type decl_kind = [
#define DECL(DERIVED, BASE) //@atd | DERIVED
#define ABSTRACT_DECL(DECL) DECL
#include <clang/AST/DeclNodes.inc>
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpDeclRef(const Decl &D) {
  const NamedDecl *ND = dyn_cast<NamedDecl>(&D);
  const ValueDecl *VD = dyn_cast<ValueDecl>(&D);
  bool IsHidden = ND && ND->isHidden();
  ObjectScope Scope(OF, 2 + (bool)ND + (bool)VD + IsHidden);

  OF.emitTag("kind");
  OF.emitSimpleVariant(D.getDeclKindName());
  OF.emitTag("decl_pointer");
  dumpPointer(&D);
  if (ND) {
    OF.emitTag("name");
    dumpName(*ND);
    OF.emitFlag("is_hidden", IsHidden);
  }
  if (VD) {
    OF.emitTag("qual_type");
    dumpQualType(VD->getType());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::DeclContextTupleSize() {
  return 2;
}
//@atd #define decl_context_tuple decl list * decl_context_info
//@atd type decl_context_info = {
//@atd   ~has_external_lexical_storage : bool;
//@atd   ~has_external_visible_storage : bool
//@atd } <ocaml field_prefix="dci_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDeclContext(const DeclContext *DC) {
  if (!DC) {
    { ArrayScope Scope(OF, 0); }
    { ObjectScope Scope(OF, 0); }
    return;
  }
  {
    std::vector<Decl *> declsToDump;
    for (auto I : DC->decls()) {
      declsToDump.push_back(I);
    }
    /* Some typedefs are not part of AST. 'instancetype' is one of them.
    Export it nevertheless as part of TranslationUnitDecl context. */
    // getObjCInstanceType() should return null type when 'instancetype' is not
    // known yet - it doesn't work this way due to bug in clang, but keep
    // the check for when the bug is fixed.
    if (isa<TranslationUnitDecl>(DC) &&
        Context.getObjCInstanceType().getTypePtrOrNull()) {
      declsToDump.push_back(Context.getObjCInstanceTypeDecl());
    }
    ArrayScope Scope(OF, declsToDump.size());
    for (auto I : declsToDump) {
      dumpDecl(I);
    }
  }
  {
    bool HasExternalLexicalStorage = DC->hasExternalLexicalStorage();
    bool HasExternalVisibleStorage = DC->hasExternalVisibleStorage();
    ObjectScope Scope(OF,
                      0 + HasExternalLexicalStorage +
                          HasExternalVisibleStorage); // not covered by tests

    OF.emitFlag("has_external_lexical_storage", HasExternalLexicalStorage);
    OF.emitFlag("has_external_visible_storage", HasExternalVisibleStorage);
  }
}

//@atd type lookups = {
//@atd   decl_ref : decl_ref;
//@atd   ?primary_context_pointer : pointer option;
//@atd   lookups : lookup list;
//@atd   ~has_undeserialized_decls : bool;
//@atd } <ocaml field_prefix="lups_">
//@atd type lookup = {
//@atd   decl_name : string;
//@atd   decl_refs : decl_ref list;
//@atd } <ocaml field_prefix="lup_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpLookups(const DeclContext &DC) {
  ObjectScope Scope(OF, 4); // not covered by tests

  OF.emitTag("decl_ref");
  dumpDeclRef(cast<Decl>(DC));

  const DeclContext *Primary = DC.getPrimaryContext();
  if (Primary != &DC) {
    OF.emitTag("primary_context_pointer");
    dumpPointer(cast<Decl>(Primary));
  }

  OF.emitTag("lookups");
  {
    ArrayScope Scope(OF);
    DeclContext::all_lookups_iterator I = Primary->noload_lookups_begin(),
                                      E = Primary->noload_lookups_end();
    while (I != E) {
      DeclarationName Name = I.getLookupName();
      DeclContextLookupResult R = *I++;

      ObjectScope Scope(OF, 2); // not covered by tests
      OF.emitTag("decl_name");
      OF.emitString(Name.getAsString());

      OF.emitTag("decl_refs");
      {
        ArrayScope Scope(OF);
        for (DeclContextLookupResult::iterator RI = R.begin(), RE = R.end();
             RI != RE;
             ++RI) {
          dumpDeclRef(**RI);
        }
      }
    }
  }

  bool HasUndeserializedLookups = Primary->hasExternalVisibleStorage();
  OF.emitFlag("has_undeserialized_decls", HasUndeserializedLookups);
}

//@atd type attribute = [
#define ATTR(X) //@atd   | X@@Attr of attribute_info
#include <clang/Basic/AttrList.inc>
//@atd ] <ocaml repr="classic">
//@atd type attribute_info = {
//@atd   pointer : pointer;
//@atd   source_range : source_range;
//@atd   parameters : string list;
//@atd   ~is_inherited : bool;
//@atd   ~is_implicit : bool
//@atd } <ocaml field_prefix="ai_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpAttr(const Attr &Att) {
  std::string tag;
  switch (Att.getKind()) {
#define ATTR(X)      \
  case attr::X:      \
    tag = #X "Attr"; \
    break;
#include <clang/Basic/AttrList.inc>
  }
  VariantScope Scope(OF, tag);
  {
    bool IsInherited = Att.isInherited();
    bool IsImplicit = Att.isImplicit();
    ObjectScope Scope(OF, 3 + IsInherited + IsImplicit);
    OF.emitTag("pointer");
    dumpPointer(&Att);
    OF.emitTag("source_range");
    dumpSourceRange(Att.getRange());

    OF.emitTag("parameters");
    {
      AttrParameterVectorStream OS{};

      // TODO: better dumping of attribute parameters.
      // Here we skip three types of parameters (see #define's below)
      // and output the others as strings, so clients reading the
      // emitted AST will have to parse them.
      const Attr *A = &Att;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#define dumpBareDeclRef(X) OS << "?"
#define dumpStmt(X) OS << "?"
#define dumpType(X) OS << "?"
#include <clang/AST/AttrDump.inc>
#undef dumpBareDeclRef
#undef dumpStmt
#undef dumpType
#pragma clang diagnostic pop

      {
        ArrayScope Scope(OF, OS.getContent().size());
        for (const std::string &item : OS.getContent()) {
          OF.emitString(item);
        }
      }
    }

    OF.emitFlag("is_inherited", IsInherited);
    OF.emitFlag("is_implicit", IsImplicit);
  }
}

//===----------------------------------------------------------------------===//
//  C++ Utilities
//===----------------------------------------------------------------------===//

//@atd type access_specifier = [ None | Public | Protected | Private ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpAccessSpecifier(AccessSpecifier AS) {
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
  case AS_none:
    OF.emitSimpleVariant("None");
    break;
  }
}

//@atd type cxx_ctor_initializer = {
//@atd   subject : cxx_ctor_initializer_subject;
//@atd   source_range : source_range;
//@atd   ?init_expr : stmt option;
//@atd } <ocaml field_prefix="xci_">
//@atd type cxx_ctor_initializer_subject = [
//@atd   Member of decl_ref
//@atd | Delegating of type_ptr
//@atd | BaseClass of (type_ptr * bool)
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpCXXCtorInitializer(
    const CXXCtorInitializer &Init) {
  const Expr *E = Init.getInit();
  ObjectScope Scope(OF, 2 + (bool)E);

  OF.emitTag("subject");
  const FieldDecl *FD = Init.getAnyMember();
  if (FD) {
    VariantScope Scope(OF, "Member");
    dumpDeclRef(*FD);
  } else if (Init.isDelegatingInitializer()) {
    VariantScope Scope(OF, "Delegating");
    dumpQualTypeNoQuals(Init.getTypeSourceInfo()->getType());
  } else {
    VariantScope Scope(OF, "BaseClass");
    {
      TupleScope Scope(OF, 2);
      dumpQualTypeNoQuals(Init.getTypeSourceInfo()->getType());
      OF.emitBoolean(Init.isBaseVirtual());
    }
  }
  OF.emitTag("source_range");
  dumpSourceRange(Init.getSourceRange());
  if (E) {
    OF.emitTag("init_expr");
    dumpStmt(E);
  }
}

//@atd type declaration_name = {
//@atd   kind : declaration_name_kind;
//@atd   name : string;
//@atd }  <ocaml field_prefix="dn_">
//@atd type declaration_name_kind = [
//@atd   Identifier
//@atd | ObjCZeroArgSelector
//@atd | ObjCOneArgSelector
//@atd | ObjCMultiArgSelector
//@atd | CXXConstructorName
//@atd | CXXDestructorName
//@atd | CXXConversionFunctionName
//@atd | CXXOperatorName
//@atd | CXXLiteralOperatorName
//@atd | CXXUsingDirective
//@atd | CXXDeductionGuideName
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpDeclarationName(const DeclarationName &Name) {
  ObjectScope Scope(OF, 2); // not covered by tests
  OF.emitTag("kind");
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
    OF.emitSimpleVariant("Identifier");
    break;
  case DeclarationName::ObjCZeroArgSelector:
    OF.emitSimpleVariant("ObjCZeroArgSelector");
    break;
  case DeclarationName::ObjCOneArgSelector:
    OF.emitSimpleVariant("ObjCOneArgSelector");
    break;
  case DeclarationName::ObjCMultiArgSelector:
    OF.emitSimpleVariant("ObjCMultiArgSelector");
    break;
  case DeclarationName::CXXConstructorName:
    OF.emitSimpleVariant("CXXConstructorName");
    break;
  case DeclarationName::CXXDestructorName:
    OF.emitSimpleVariant("CXXDestructorName");
    break;
  case DeclarationName::CXXConversionFunctionName:
    OF.emitSimpleVariant("CXXConversionFunctionName");
    break;
  case DeclarationName::CXXOperatorName:
    OF.emitSimpleVariant("CXXOperatorName");
    break;
  case DeclarationName::CXXLiteralOperatorName:
    OF.emitSimpleVariant("CXXLiteralOperatorName");
    break;
  case DeclarationName::CXXUsingDirective:
    OF.emitSimpleVariant("CXXUsingDirective");
    break;
  case DeclarationName::CXXDeductionGuideName:
    OF.emitSimpleVariant("CXXDeductionGuideName");
    break;
  }
  OF.emitTag("name");
  OF.emitString(Name.getAsString());
}
//@atd type nested_name_specifier_loc = {
//@atd   kind : specifier_kind;
//@atd   ?ref : decl_ref option;
//@atd } <ocaml field_prefix="nnsl_">
//@atd type specifier_kind = [
//@atd    Identifier
//@atd  | Namespace
//@atd  | NamespaceAlias
//@atd  | TypeSpec
//@atd  | TypeSpecWithTemplate
//@atd  | Global
//@atd  | Super
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpNestedNameSpecifierLoc(
    NestedNameSpecifierLoc NNS) {
  SmallVector<NestedNameSpecifierLoc, 8> NestedNames;
  while (NNS) {
    NestedNames.push_back(NNS);
    NNS = NNS.getPrefix();
  }
  ArrayScope Scope(OF, NestedNames.size());
  while (!NestedNames.empty()) {
    NNS = NestedNames.pop_back_val();
    NestedNameSpecifier::SpecifierKind Kind =
        NNS.getNestedNameSpecifier()->getKind();
    ObjectScope Scope(OF, 2);
    OF.emitTag("kind");
    switch (Kind) {
    case NestedNameSpecifier::Identifier:
      OF.emitSimpleVariant("Identifier");
      break;
    case NestedNameSpecifier::Namespace:
      OF.emitSimpleVariant("Namespace");
      OF.emitTag("ref");
      dumpDeclRef(*NNS.getNestedNameSpecifier()->getAsNamespace());
      break;
    case NestedNameSpecifier::NamespaceAlias:
      OF.emitSimpleVariant("NamespaceAlias");
      OF.emitTag("ref");
      dumpDeclRef(*NNS.getNestedNameSpecifier()->getAsNamespaceAlias());
      break;
    case NestedNameSpecifier::TypeSpec:
      OF.emitSimpleVariant("TypeSpec");
      break;
    case NestedNameSpecifier::TypeSpecWithTemplate:
      OF.emitSimpleVariant("TypeSpecWithTemplate");
      break;
    case NestedNameSpecifier::Global:
      OF.emitSimpleVariant("Global");
      break;
    case NestedNameSpecifier::Super:
      OF.emitSimpleVariant("Super");
      break;
    }
  }
}

// template <class ATDWriter>
// void ASTExporter<ATDWriter>::dumpTemplateParameters(const
// TemplateParameterList *TPL) {
//  if (!TPL)
//    return;
//
//  for (TemplateParameterList::const_iterator I = TPL->begin(), E = TPL->end();
//       I != E; ++I)
//    dumpDecl(*I);
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::dumpTemplateArgumentListInfo(
//    const TemplateArgumentListInfo &TALI) {
//  for (unsigned i = 0, e = TALI.size(); i < e; ++i) {
//    dumpTemplateArgumentLoc(TALI[i]);
//  }
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::dumpTemplateArgumentLoc(const
// TemplateArgumentLoc &A) {
//  dumpTemplateArgument(A.getArgument(), A.getSourceRange());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::dumpTemplateArgumentList(const
// TemplateArgumentList &TAL) {
//  for (unsigned i = 0, e = TAL.size(); i < e; ++i)
//    dumpTemplateArgument(TAL[i]);
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::dumpTemplateArgument(const TemplateArgument &A,
// SourceRange R) {
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
//    dumpStmt(A.getAsExpr());
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

template <class ATDWriter>
bool ASTExporter<ATDWriter>::alwaysEmitParent(const Decl *D) {
  if (isa<ObjCMethodDecl>(D) || isa<CXXMethodDecl>(D) || isa<FieldDecl>(D) ||
      isa<ObjCIvarDecl>(D)) {
    return true;
  }
  return false;
}
//===----------------------------------------------------------------------===//
//  Decl dumping methods.
//===----------------------------------------------------------------------===//

#define DECL(DERIVED, BASE) //@atd #define @DERIVED@_decl_tuple @BASE@_tuple
#define ABSTRACT_DECL(DECL) DECL
#include <clang/AST/DeclNodes.inc>
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpDecl(const Decl *D) {
  if (!D) {
    // We use a fixed EmptyDecl node to represent null pointers
    D = NullPtrDecl;
  }
  VariantScope Scope(OF, std::string(D->getDeclKindName()) + "Decl");
  {
    TupleScope Scope(OF, ASTExporter::tupleSizeOfDeclKind(D->getKind()));
    ConstDeclVisitor<ASTExporter<ATDWriter>>::Visit(D);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::DeclTupleSize() {
  return 1;
}

//@atd #define decl_tuple decl_info
//@atd type decl_info = {
//@atd   pointer : pointer;
//@atd   ?parent_pointer : pointer option;
//@atd   source_range : source_range;
//@atd   ?owning_module : string option;
//@atd   ~is_hidden : bool;
//@atd   ~is_implicit : bool;
//@atd   ~is_used : bool;
//@atd   ~is_this_declaration_referenced : bool;
//@atd   ~is_invalid_decl : bool;
//@atd   ~attributes : attribute list;
//@atd   ?full_comment : comment option;
//@atd   ~access <ocaml default="`None"> : access_specifier
//@atd } <ocaml field_prefix="di_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDecl(const Decl *D) {
  {
    bool ShouldEmitParentPointer =
        alwaysEmitParent(D) ||
        D->getLexicalDeclContext() != D->getDeclContext();
    Module *M = D->getImportedOwningModule();
    if (!M) {
      M = D->getLocalOwningModule();
    }
    const NamedDecl *ND = dyn_cast<NamedDecl>(D);
    bool IsNDHidden = ND && ND->isHidden();
    bool IsDImplicit = D->isImplicit();
    bool IsDUsed = D->isUsed();
    bool IsDReferenced = D->isThisDeclarationReferenced();
    bool IsDInvalid = D->isInvalidDecl();
    bool HasAttributes = D->hasAttrs();
    const FullComment *Comment =
        D->getASTContext().getLocalCommentForDeclUncached(D);
    AccessSpecifier Access = D->getAccess();
    bool HasAccess = Access != AccessSpecifier::AS_none;
    int maxSize = 3 + ShouldEmitParentPointer + (bool)M + IsNDHidden +
                  IsDImplicit + IsDUsed + IsDReferenced + IsDInvalid +
                  HasAttributes + (bool)Comment + HasAccess;
    ObjectScope Scope(OF, maxSize);

    OF.emitTag("pointer");
    dumpPointer(D);
    if (ShouldEmitParentPointer) {
      OF.emitTag("parent_pointer");
      dumpPointer(cast<Decl>(D->getDeclContext()));
    }

    OF.emitTag("source_range");
    dumpSourceRange(D->getSourceRange());
    if (M) {
      OF.emitTag("owning_module");
      OF.emitString(M->getFullModuleName());
    }
    OF.emitFlag("is_hidden", IsNDHidden);
    OF.emitFlag("is_implicit", IsDImplicit);
    OF.emitFlag("is_used", IsDUsed);
    OF.emitFlag("is_this_declaration_referenced", IsDReferenced);
    OF.emitFlag("is_invalid_decl", IsDInvalid);

    if (HasAttributes) {
      OF.emitTag("attributes");
      ArrayScope ArrayAttr(OF, D->getAttrs().size());
      for (auto I : D->attrs()) {
        dumpAttr(*I);
      }
    }

    if (Comment && Options.dumpComments) {
      OF.emitTag("full_comment");
      dumpFullComment(Comment);
    }

    if (HasAccess) {
      OF.emitTag("access");
      dumpAccessSpecifier(Access);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CapturedDeclTupleSize() {
  return DeclTupleSize() + DeclContextTupleSize();
}
//@atd #define captured_decl_tuple decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCapturedDecl(const CapturedDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::LinkageSpecDeclTupleSize() {
  return DeclTupleSize() + DeclContextTupleSize();
}
//@atd #define linkage_spec_decl_tuple decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitLinkageSpecDecl(const LinkageSpecDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::NamespaceDeclTupleSize() {
  return NamedDeclTupleSize() + DeclContextTupleSize() + 1;
}
//@atd #define namespace_decl_tuple named_decl_tuple * decl_context_tuple * namespace_decl_info
//@atd type namespace_decl_info = {
//@atd   ~is_inline : bool;
//@atd   ?original_namespace : decl_ref option;
//@atd } <ocaml field_prefix="ndi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitNamespaceDecl(const NamespaceDecl *D) {
  VisitNamedDecl(D);
  VisitDeclContext(D);

  bool IsInline = D->isInline();
  bool IsOriginalNamespace = D->isOriginalNamespace();
  ObjectScope Scope(OF, 0 + IsInline + !IsOriginalNamespace);

  OF.emitFlag("is_inline", IsInline);
  if (!IsOriginalNamespace) {
    OF.emitTag("original_namespace");
    dumpDeclRef(*D->getOriginalNamespace());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCContainerDeclTupleSize() {
  return NamedDeclTupleSize() + DeclContextTupleSize();
}
//@atd #define obj_c_container_decl_tuple named_decl_tuple * decl_context_tuple
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCContainerDecl(
    const ObjCContainerDecl *D) {
  VisitNamedDecl(D);
  VisitDeclContext(D);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TagDeclTupleSize() {
  return TypeDeclTupleSize() + DeclContextTupleSize() + 1;
}
//@atd type tag_kind = [
//@atd   TTK_Struct
//@atd | TTK_Interface
//@atd | TTK_Union
//@atd | TTK_Class
//@atd | TTK_Enum
//@atd ]
//@atd #define tag_decl_tuple type_decl_tuple * decl_context_tuple * tag_kind
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTagDecl(const TagDecl *D) {
  VisitTypeDecl(D);
  VisitDeclContext(D);
  switch (D->getTagKind()) {
  case TagTypeKind::TTK_Struct:
    OF.emitSimpleVariant("TTK_Struct");
    break;
  case TagTypeKind::TTK_Interface:
    OF.emitSimpleVariant("TTK_Interface");
    break;
  case TagTypeKind::TTK_Union:
    OF.emitSimpleVariant("TTK_Union");
    break;
  case TagTypeKind::TTK_Class:
    OF.emitSimpleVariant("TTK_Class");
    break;
  case TagTypeKind::TTK_Enum:
    OF.emitSimpleVariant("TTK_Enum");
    break;
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TypeDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define type_decl_tuple named_decl_tuple * type_ptr
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTypeDecl(const TypeDecl *D) {
  VisitNamedDecl(D);
  const Type *T = D->getTypeForDecl();
  dumpPointerToType(T);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ValueDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define value_decl_tuple named_decl_tuple * qual_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitValueDecl(const ValueDecl *D) {
  VisitNamedDecl(D);
  dumpQualType(D->getType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TranslationUnitDeclTupleSize() {
  return DeclTupleSize() + DeclContextTupleSize() + 1;
}
//@atd type input_kind = [
//@atd   IK_None
//@atd | IK_Asm
//@atd | IK_C
//@atd | IK_CXX
//@atd | IK_ObjC
//@atd | IK_ObjCXX
//@atd | IK_OpenCL
//@atd | IK_CUDA
//@atd | IK_RenderScript
//@atd | IK_LLVM_IR
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpInputKind(InputKind kind) {
  // Despite here we deal only with the language field of InputKind, there are
  // new info in InputKind that can still be used, e.g. whether the source is
  // preprocessed (PP), or precompiled.
  switch (kind.getLanguage()) {
  case InputKind::Unknown:
    OF.emitSimpleVariant("IK_None");
    break;
  case InputKind::Asm:
    OF.emitSimpleVariant("IK_Asm");
    break;
  case InputKind::C:
    OF.emitSimpleVariant("IK_C");
    break;
  case InputKind::CXX:
    OF.emitSimpleVariant("IK_CXX");
    break;
  case InputKind::ObjC:
    OF.emitSimpleVariant("IK_ObjC");
    break;
  case InputKind::ObjCXX:
    OF.emitSimpleVariant("IK_ObjCXX");
    break;
  case InputKind::OpenCL:
    OF.emitSimpleVariant("IK_OpenCL");
    break;
  case InputKind::CUDA:
    OF.emitSimpleVariant("IK_CUDA");
    break;
  case InputKind::RenderScript:
    OF.emitSimpleVariant("IK_RenderScript");
    break;
  case InputKind::LLVM_IR:
    OF.emitSimpleVariant("IK_LLVM_IR");
    break;
  }
}
//@atd #define translation_unit_decl_tuple decl_tuple * decl_context_tuple * translation_unit_decl_info
//@atd type  translation_unit_decl_info = {
//@atd   input_path : source_file;
//@atd   input_kind : input_kind;
//@atd   ~arc_enabled : bool;
//@atd   types : c_type list;
//@atd } <ocaml field_prefix="tudi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTranslationUnitDecl(
    const TranslationUnitDecl *D) {
  VisitDecl(D);
  VisitDeclContext(D);
  bool IsARCEnabled = Context.getLangOpts().ObjCAutoRefCount;
  ObjectScope Scope(OF, 3 + IsARCEnabled);
  OF.emitTag("input_path");
  OF.emitString(
      Options.normalizeSourcePath(Options.inputFile.getFile().str().c_str()));
  OF.emitTag("input_kind");
  dumpInputKind(Options.inputFile.getKind());
  OF.emitFlag("arc_enabled", IsARCEnabled);
  OF.emitTag("types");
  const auto &types = Context.getTypes();
  ArrayScope aScope(OF, types.size() + 1); // + 1 for nullptr
  for (const Type *type : types) {
    dumpType(type);
  }
  // Just in case, add NoneType to dumped types
  dumpType(nullptr);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::NamedDeclTupleSize() {
  return DeclTupleSize() + 1;
}
//@atd #define named_decl_tuple decl_tuple * named_decl_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitNamedDecl(const NamedDecl *D) {
  VisitDecl(D);
  dumpName(*D);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TypedefDeclTupleSize() {
  return ASTExporter::TypedefNameDeclTupleSize() + 1;
}
//@atd #define typedef_decl_tuple typedef_name_decl_tuple * typedef_decl_info
//@atd type typedef_decl_info = {
//@atd   ~is_module_private : bool
//@atd } <ocaml field_prefix="tdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTypedefDecl(const TypedefDecl *D) {
  ASTExporter<ATDWriter>::VisitTypedefNameDecl(D);

  bool IsModulePrivate = D->isModulePrivate();
  ObjectScope Scope(OF, 0 + IsModulePrivate);

  OF.emitFlag("is_module_private", IsModulePrivate);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::EnumDeclTupleSize() {
  return TagDeclTupleSize() + 1;
}
//@atd #define enum_decl_tuple tag_decl_tuple * enum_decl_info
//@atd type enum_decl_info = {
//@atd   ?scope : enum_decl_scope option;
//@atd   ~is_module_private : bool
//@atd } <ocaml field_prefix="edi_">
//@atd type enum_decl_scope = [Class | Struct]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitEnumDecl(const EnumDecl *D) {
  VisitTagDecl(D);

  bool IsScoped = D->isScoped();
  bool IsModulePrivate = D->isModulePrivate();
  ObjectScope Scope(OF, 0 + IsScoped + IsModulePrivate); // not covered by tests

  if (IsScoped) {
    OF.emitTag("scope");
    if (D->isScopedUsingClassTag())
      OF.emitSimpleVariant("Class");
    else
      OF.emitSimpleVariant("Struct");
  }
  OF.emitFlag("is_module_private", IsModulePrivate);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::RecordDeclTupleSize() {
  return TagDeclTupleSize() + 1;
}
//@atd #define record_decl_tuple tag_decl_tuple * record_decl_info
//@atd type record_decl_info = {
//@atd   definition_ptr : pointer;
//@atd   ~is_module_private : bool;
//@atd   ~is_complete_definition : bool;
//@atd   ~is_dependent_type : bool;
//@atd } <ocaml field_prefix="rdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitRecordDecl(const RecordDecl *D) {
  VisitTagDecl(D);

  bool IsModulePrivate = D->isModulePrivate();
  bool IsCompleteDefinition = D->isCompleteDefinition();
  bool IsDependentType = D->isDependentType();
  ObjectScope Scope(OF, 1 + IsModulePrivate + IsCompleteDefinition + IsDependentType);
  OF.emitTag("definition_ptr");
  dumpPointer(D->getDefinition());
  OF.emitFlag("is_module_private", IsModulePrivate);
  OF.emitFlag("is_complete_definition", IsCompleteDefinition);
  OF.emitFlag("is_dependent_type", IsDependentType);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::EnumConstantDeclTupleSize() {
  return ValueDeclTupleSize() + 1;
}
//@atd #define enum_constant_decl_tuple value_decl_tuple * enum_constant_decl_info
//@atd type enum_constant_decl_info = {
//@atd   ?init_expr : stmt option
//@atd } <ocaml field_prefix="ecdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitEnumConstantDecl(const EnumConstantDecl *D) {
  VisitValueDecl(D);

  const Expr *Init = D->getInitExpr();
  ObjectScope Scope(OF, 0 + (bool)Init); // not covered by tests

  if (Init) {
    OF.emitTag("init_expr");
    dumpStmt(Init);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::IndirectFieldDeclTupleSize() {
  return ValueDeclTupleSize() + 1;
}
//@atd #define indirect_field_decl_tuple value_decl_tuple * decl_ref list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitIndirectFieldDecl(
    const IndirectFieldDecl *D) {
  VisitValueDecl(D);
  ArrayScope Scope(
      OF,
      std::distance(D->chain_begin(), D->chain_end())); // not covered by tests
  for (auto I : D->chain()) {
    dumpDeclRef(*I);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FunctionDeclTupleSize() {
  return ASTExporter::DeclaratorDeclTupleSize() + 1;
}
//@atd #define function_decl_tuple declarator_decl_tuple * function_decl_info
//@atd type function_decl_info = {
//@atd   ?mangled_name : string option;
//@atd   ?storage_class : string option;
//@atd   ~is_inline : bool;
//@atd   ~is_module_private : bool;
//@atd   ~is_pure : bool;
//@atd   ~is_delete_as_written : bool;
//@atd   ~is_no_throw : bool;
//@atd   ~parameters : decl list;
//@atd   ?decl_ptr_with_body : pointer option;
//@atd   ?body : stmt option;
//@atd   ?template_specialization : template_specialization_info option
//@atd } <ocaml field_prefix="fdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFunctionDecl(const FunctionDecl *D) {
  ASTExporter<ATDWriter>::VisitDeclaratorDecl(D);
  // We purposedly do not call VisitDeclContext(D).

  bool ShouldMangleName = Mangler->shouldMangleDeclName(D);
  StorageClass SC = D->getStorageClass();
  bool HasStorageClass = SC != SC_None;
  bool IsInlineSpecified = D->isInlineSpecified();
  bool IsModulePrivate = D->isModulePrivate();
  bool IsPure = D->isPure();
  bool IsDeletedAsWritten = D->isDeletedAsWritten();

  const FunctionProtoType *FPT = D->getType()->getAs<FunctionProtoType>();
  // FunctionProtoType::canThrow is more informative, consider using
  // CanThrowResult type instead
  // https://github.com/llvm-mirror/clang/commit/ce58cd720b070c4481f32911d5d9c66411963ca6
  auto IsNoThrow = FPT ? FPT->isNothrow(Context) : false;
  const FunctionDecl *DeclWithBody = D;
  // FunctionDecl::hasBody() will set DeclWithBody pointer to decl that
  // has body. If there is no body in all decls of that function,
  // then we need to set DeclWithBody to nullptr manually
  if (!D->hasBody(DeclWithBody)) {
    DeclWithBody = nullptr;
  }
  bool HasDeclarationBody = D->doesThisDeclarationHaveABody();
  FunctionTemplateDecl *TemplateDecl = D->getPrimaryTemplate();
  // suboptimal: decls_in_prototype_scope and parameters not taken into account
  // accurately
  int size = 2 + ShouldMangleName + HasStorageClass + IsInlineSpecified +
             IsModulePrivate + IsPure + IsDeletedAsWritten + IsNoThrow +
             HasDeclarationBody + (bool)DeclWithBody + (bool)TemplateDecl;
  ObjectScope Scope(OF, size);

  if (ShouldMangleName) {
    OF.emitTag("mangled_name");
    SmallString<64> Buf;
    llvm::raw_svector_ostream StrOS(Buf);
    if (const auto *CD = dyn_cast<CXXConstructorDecl>(D)) {
      Mangler->mangleCXXCtor(CD, Ctor_Complete, StrOS);
    } else if (const auto *DD = dyn_cast<CXXDestructorDecl>(D)) {
      Mangler->mangleCXXDtor(DD, Dtor_Deleting, StrOS);
    } else {
      Mangler->mangleName(D, StrOS);
    }
    // mangled names can get ridiculously long, so hash them to a fixed size
    OF.emitString(std::to_string(fnv64Hash(StrOS)));
  }

  if (HasStorageClass) {
    OF.emitTag("storage_class");
    OF.emitString(VarDecl::getStorageClassSpecifierString(SC));
  }

  OF.emitFlag("is_inline", IsInlineSpecified);
  OF.emitFlag("is_module_private", IsModulePrivate);
  OF.emitFlag("is_pure", IsPure);
  OF.emitFlag("is_delete_as_written", IsDeletedAsWritten);
  OF.emitFlag("is_no_throw", IsNoThrow);

  //  if (const FunctionProtoType *FPT =
  //  D->getType()->getAs<FunctionProtoType>()) {
  //    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
  //    switch (EPI.ExceptionSpec.Type) {
  //    default: break;
  //    case EST_Unevaluated:
  //      OS << " noexcept-unevaluated " << EPI.ExceptionSpec.SourceDecl;
  //      break;
  //    case EST_Uninstantiated:
  //      OS << " noexcept-uninstantiated " << EPI.ExceptionSpec.SourceTemplate;
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
    FunctionDecl::param_const_iterator I = D->param_begin(), E = D->param_end();
    if (I != E) {
      OF.emitTag("parameters");
      ArrayScope Scope(OF, std::distance(I, E));
      for (; I != E; ++I) {
        dumpDecl(*I);
      }
    }
  }

  if (DeclWithBody) {
    OF.emitTag("decl_ptr_with_body");
    dumpPointer(DeclWithBody);
  }

  if (HasDeclarationBody) {
    const Stmt *Body = D->getBody();
    if (Body) {
      OF.emitTag("body");
      dumpStmt(Body);
    }
  }
  if (TemplateDecl) {
    OF.emitTag("template_specialization");
    dumpTemplateSpecialization(TemplateDecl,
                               *D->getTemplateSpecializationArgs());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FieldDeclTupleSize() {
  return ASTExporter::DeclaratorDeclTupleSize() + 1;
}
//@atd #define field_decl_tuple declarator_decl_tuple * field_decl_info
//@atd type field_decl_info = {
//@atd   ~is_mutable : bool;
//@atd   ~is_module_private : bool;
//@atd   ?init_expr : stmt option;
//@atd   ?bit_width_expr : stmt option
//@atd } <ocaml field_prefix="fldi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFieldDecl(const FieldDecl *D) {
  ASTExporter<ATDWriter>::VisitDeclaratorDecl(D);

  bool IsMutable = D->isMutable();
  bool IsModulePrivate = D->isModulePrivate();
  bool HasBitWidth = D->isBitField() && D->getBitWidth();
  Expr *Init = D->getInClassInitializer();
  ObjectScope Scope(OF,
                    0 + IsMutable + IsModulePrivate + HasBitWidth +
                        (bool)Init); // not covered by tests

  OF.emitFlag("is_mutable", IsMutable);
  OF.emitFlag("is_module_private", IsModulePrivate);

  if (HasBitWidth) {
    OF.emitTag("bit_width_expr");
    dumpStmt(D->getBitWidth());
  }

  if (Init) {
    OF.emitTag("init_expr");
    dumpStmt(Init);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::VarDeclTupleSize() {
  return ASTExporter::DeclaratorDeclTupleSize() + 1;
}
//@atd #define var_decl_tuple declarator_decl_tuple * var_decl_info
//@atd type var_decl_info = {
//@atd   ?storage_class : string option;
//@atd   ~tls_kind <ocaml default="`Tls_none">: tls_kind;
//@atd   ~is_global : bool;
//@atd   ~is_static_local : bool;
//@atd   ~is_module_private : bool;
//@atd   ~is_nrvo_variable : bool;
//@atd   ~is_const_expr : bool;
//@atd   ?init_expr : stmt option;
//@atd   ?parm_index_in_function : int option;
//@atd } <ocaml field_prefix="vdi_">
//@atd type tls_kind = [ Tls_none | Tls_static | Tls_dynamic ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitVarDecl(const VarDecl *D) {
  ASTExporter<ATDWriter>::VisitDeclaratorDecl(D);

  StorageClass SC = D->getStorageClass();
  bool HasStorageClass = SC != SC_None;
  bool IsGlobal = D->hasGlobalStorage(); // including static function variables
  bool IsStaticLocal = D->isStaticLocal(); // static function variables
  bool IsModulePrivate = D->isModulePrivate();
  bool IsNRVOVariable = D->isNRVOVariable();
  bool IsConstExpr = D->isConstexpr();
  bool HasInit = D->hasInit();
  const ParmVarDecl *ParmDecl = dyn_cast<ParmVarDecl>(D);
  bool HasParmIndex = (bool)ParmDecl;
  // suboptimal: tls_kind is not taken into account accurately
  ObjectScope Scope(OF,
                    1 + HasStorageClass + IsGlobal + IsStaticLocal +
                        IsModulePrivate + IsNRVOVariable + IsConstExpr +
                        HasInit + HasParmIndex);

  if (HasStorageClass) {
    OF.emitTag("storage_class");
    OF.emitString(VarDecl::getStorageClassSpecifierString(SC));
  }

  switch (D->getTLSKind()) {
  case VarDecl::TLS_None:
    break;
  case VarDecl::TLS_Static:
    OF.emitTag("tls_kind");
    OF.emitSimpleVariant("Tls_static");
    break;
  case VarDecl::TLS_Dynamic:
    OF.emitTag("tls_kind");
    OF.emitSimpleVariant("Tls_dynamic");
    break;
  }

  OF.emitFlag("is_global", IsGlobal);
  OF.emitFlag("is_static_local", IsStaticLocal);
  OF.emitFlag("is_module_private", IsModulePrivate);
  OF.emitFlag("is_nrvo_variable", IsNRVOVariable);
  OF.emitFlag("is_const_expr", IsConstExpr);
  if (HasInit) {
    OF.emitTag("init_expr");
    dumpStmt(D->getInit());
  }
  if (HasParmIndex) {
    OF.emitTag("parm_index_in_function");
    OF.emitInteger(ParmDecl->getFunctionScopeIndex());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FileScopeAsmDeclTupleSize() {
  return DeclTupleSize() + 1;
}
//@atd #define file_scope_asm_decl_tuple decl_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFileScopeAsmDecl(const FileScopeAsmDecl *D) {
  VisitDecl(D);
  OF.emitString(D->getAsmString()->getBytes());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ImportDeclTupleSize() {
  return DeclTupleSize() + 1;
}
//@atd #define import_decl_tuple decl_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitImportDecl(const ImportDecl *D) {
  VisitDecl(D);
  OF.emitString(D->getImportedModule()->getFullModuleName());
}

//===----------------------------------------------------------------------===//
// C++ Declarations
//===----------------------------------------------------------------------===//

template <class ATDWriter>
int ASTExporter<ATDWriter>::UsingDirectiveDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define using_directive_decl_tuple named_decl_tuple * using_directive_decl_info
//@atd type using_directive_decl_info = {
//@atd   using_location : source_location;
//@atd   namespace_key_location : source_location;
//@atd   nested_name_specifier_locs : nested_name_specifier_loc list;
//@atd   ?nominated_namespace : decl_ref option;
//@atd } <ocaml field_prefix="uddi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitUsingDirectiveDecl(
    const UsingDirectiveDecl *D) {
  VisitNamedDecl(D);

  bool HasNominatedNamespace = D->getNominatedNamespace();
  ObjectScope Scope(OF, 3 + HasNominatedNamespace);

  OF.emitTag("using_location");
  dumpSourceLocation(D->getUsingLoc());
  OF.emitTag("namespace_key_location");
  dumpSourceLocation(D->getNamespaceKeyLocation());
  OF.emitTag("nested_name_specifier_locs");
  dumpNestedNameSpecifierLoc(D->getQualifierLoc());
  if (HasNominatedNamespace) {
    OF.emitTag("nominated_namespace");
    dumpDeclRef(*D->getNominatedNamespace());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::NamespaceAliasDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define namespace_alias_decl_tuple named_decl_tuple * namespace_alias_decl_info
//@atd type namespace_alias_decl_info = {
//@atd   namespace_loc : source_location;
//@atd   target_name_loc : source_location;
//@atd   nested_name_specifier_locs : nested_name_specifier_loc list;
//@atd   namespace : decl_ref;
//@atd } <ocaml field_prefix="nadi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitNamespaceAliasDecl(
    const NamespaceAliasDecl *D) {
  VisitNamedDecl(D);
  ObjectScope Scope(OF, 4);
  OF.emitTag("namespace_loc");
  dumpSourceLocation(D->getNamespaceLoc());
  OF.emitTag("target_name_loc");
  dumpSourceLocation(D->getTargetNameLoc());
  OF.emitTag("nested_name_specifier_locs");
  dumpNestedNameSpecifierLoc(D->getQualifierLoc());
  OF.emitTag("namespace");
  dumpDeclRef(*D->getNamespace());
}

//@atd type lambda_capture_info = {
//@atd   capture_kind : lambda_capture_kind;
//@atd   ~capture_this : bool;
//@atd   ~capture_variable : bool;
//@atd   ~capture_VLAtype : bool;
//@atd   ?init_captured_vardecl : decl option;
//@atd   ?captured_var : decl_ref option;
//@atd   ~is_implicit : bool;
//@atd   location : source_range;
//@atd   ~is_pack_expansion: bool;
//@atd } <ocaml field_prefix="lci_">
//@atd type lambda_capture_kind = [
//@atd         | LCK_This
//@atd         | LCK_ByCopy
//@atd         | LCK_ByRef
//@atd         | LCK_VLAType
//@atd         | LCK_StarThis]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpClassLambdaCapture(const LambdaCapture *C) {

  LambdaCaptureKind CK = C->getCaptureKind();
  bool CapturesThis = C->capturesThis();
  bool CapturesVariable = C->capturesVariable();
  bool CapturesVLAType = C->capturesVLAType();
  VarDecl *decl = C->capturesVariable() ? C->getCapturedVar() : nullptr;
  bool IsInitCapture = decl && decl->isInitCapture();
  bool IsImplicit = C->isImplicit();
  SourceRange source_range = C->getLocation();
  bool IsPackExpansion = C->isPackExpansion();
  ObjectScope Scope(OF,
                    2 + CapturesThis + CapturesVariable + CapturesVLAType +
                        IsInitCapture + (bool)decl + IsImplicit +
                        IsPackExpansion);
  OF.emitTag("capture_kind");
  switch (CK) {
  case LCK_This:
    OF.emitSimpleVariant("LCK_This");
    break;
  case LCK_ByCopy:
    OF.emitSimpleVariant("LCK_ByCopy");
    break;
  case LCK_ByRef:
    OF.emitSimpleVariant("LCK_ByRef");
    break;
  case LCK_VLAType:
    OF.emitSimpleVariant("LCK_VLAType");
    break;
  case LCK_StarThis:
    OF.emitSimpleVariant("LCK_StarThis");
    break;
  };
  OF.emitFlag("capture_this", CapturesThis);
  OF.emitFlag("capture_variable", CapturesVariable);
  OF.emitFlag("capture_VLAtype", CapturesVLAType);
  if (decl) {
    if (IsInitCapture) {
      OF.emitTag("init_captured_vardecl");
      dumpDecl(decl);
    }
    OF.emitTag("captured_var");
    dumpDeclRef(*decl);
  }
  OF.emitFlag("is_implicit", IsImplicit);
  OF.emitTag("location");
  dumpSourceRange(source_range);
  OF.emitFlag("is_pack_expansion", IsPackExpansion);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXRecordDeclTupleSize() {
  return RecordDeclTupleSize() + 1;
}
//@atd #define cxx_record_decl_tuple record_decl_tuple * cxx_record_decl_info
//@atd type cxx_record_decl_info = {
//@atd   ~bases : type_ptr list;
//@atd   ~vbases : type_ptr list;
//@atd   ~transitive_vbases : type_ptr list;
//@atd   ~is_pod : bool;
//@atd   ?destructor : decl_ref option;
//@atd   ?lambda_call_operator : decl_ref option;
//@atd   ~lambda_captures : lambda_capture_info list;
//@atd } <ocaml field_prefix="xrdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXRecordDecl(const CXXRecordDecl *D) {
  VisitRecordDecl(D);

  if (!D->isCompleteDefinition()) {
    // We need to return early here. Otherwise plugin will crash.
    // It looks like CXXRecordDecl may be initialized with garbage.
    // Not sure what to do when we'll have some non-optional data to generate??
    ObjectScope Scope(OF, 0);
    return;
  }

  // getNumBases and getNumVBases are not reliable, extract this info
  // directly from what is going to be dumped
  SmallVector<CXXBaseSpecifier, 2> nonVBases;
  SmallVector<CXXBaseSpecifier, 2> vBases;
  for (const auto base : D->bases()) {
    if (base.isVirtual()) {
      vBases.push_back(base);
    } else {
      nonVBases.push_back(base);
    }
  }

  bool HasVBases = vBases.size() > 0;
  bool HasNonVBases = nonVBases.size() > 0;
  unsigned numTransitiveVBases = D->getNumVBases();
  bool HasTransitiveVBases = numTransitiveVBases > 0;
  bool IsPOD = D->isPOD();
  const CXXDestructorDecl *DestructorDecl = D->getDestructor();
  const CXXMethodDecl *LambdaCallOperator = D->getLambdaCallOperator();

  auto I = D->captures_begin(), E = D->captures_end();
  ObjectScope Scope(OF,
                    0 + HasNonVBases + HasVBases + HasTransitiveVBases + IsPOD +
                        (bool)DestructorDecl + (bool)LambdaCallOperator +
                        (I != E));

  if (HasNonVBases) {
    OF.emitTag("bases");
    ArrayScope aScope(OF, nonVBases.size());
    for (const auto base : nonVBases) {
      dumpQualTypeNoQuals(base.getType());
    }
  }
  if (HasVBases) {
    OF.emitTag("vbases");
    ArrayScope aScope(OF, vBases.size());
    for (const auto base : vBases) {
      dumpQualTypeNoQuals(base.getType());
    }
  }
  if (HasTransitiveVBases) {
    OF.emitTag("transitive_vbases");
    ArrayScope aScope(OF, numTransitiveVBases);
    for (const auto base : D->vbases()) {
      dumpQualTypeNoQuals(base.getType());
    }
  }
  OF.emitFlag("is_pod", IsPOD);

  if (DestructorDecl) {
    OF.emitTag("destructor");
    dumpDeclRef(*DestructorDecl);
  }

  if (LambdaCallOperator) {
    OF.emitTag("lambda_call_operator");
    dumpDeclRef(*LambdaCallOperator);
  }

  if (I != E) {
    OF.emitTag("lambda_captures");
    ArrayScope Scope(OF, std::distance(I, E));
    for (; I != E; ++I) {
      dumpClassLambdaCapture(I);
    }
  }
}

//@atd type template_instantiation_arg_info = [
//@atd   | Null
//@atd   | Type of qual_type
//@atd   | Declaration of pointer
//@atd   | NullPtr
//@atd   | Integral of string
//@atd   | Template
//@atd   | TemplateExpansion
//@atd   | Expression
//@atd   | Pack of template_instantiation_arg_info list
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpTemplateArgument(const TemplateArgument &Arg) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    OF.emitSimpleVariant("Null");
    break;
  case TemplateArgument::Type: {
    VariantScope Scope(OF, "Type");
    dumpQualType(Arg.getAsType());
    break;
  }
  case TemplateArgument::Declaration: {
    VariantScope Scope(OF, "Declaration");
    dumpPointer(Arg.getAsDecl());
    break;
  }
  case TemplateArgument::NullPtr:
    OF.emitSimpleVariant("NullPtr");
    break;
  case TemplateArgument::Integral: {
    VariantScope Scope(OF, "Integral");
    OF.emitString(Arg.getAsIntegral().toString(10));
    break;
  }
  case TemplateArgument::Template: {
    OF.emitSimpleVariant("Template");
    break;
  }
  case TemplateArgument::TemplateExpansion: {
    OF.emitSimpleVariant("TemplateExpansion");
    break;
  }
  case TemplateArgument::Expression: {
    OF.emitSimpleVariant("Expression");
    break;
  }
  case TemplateArgument::Pack: {
    VariantScope Scope(OF, "Pack");
    ArrayScope aScope(OF, Arg.pack_size());
    for (TemplateArgument::pack_iterator I = Arg.pack_begin(),
                                         E = Arg.pack_end();
         I != E;
         ++I) {
      dumpTemplateArgument(*I);
    }
    break;
  }
  }
}

//@atd type template_specialization_info = {
//@atd   template_decl : pointer;
//@atd   ~specialization_args : template_instantiation_arg_info list;
//@atd } <ocaml field_prefix="tsi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpTemplateSpecialization(
    const TemplateDecl *D, const TemplateArgumentList &Args) {
  bool HasTemplateArgs = Args.size() > 0;
  ObjectScope oScope(OF, 1 + HasTemplateArgs);
  OF.emitTag("template_decl");
  dumpPointer(D);
  if (HasTemplateArgs) {
    OF.emitTag("specialization_args");
    ArrayScope aScope(OF, Args.size());
    for (size_t i = 0; i < Args.size(); i++) {
      dumpTemplateArgument(Args[i]);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ClassTemplateSpecializationDeclTupleSize() {
  return CXXRecordDeclTupleSize() + 2;
}

//@atd #define class_template_specialization_decl_tuple cxx_record_decl_tuple * string * template_specialization_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitClassTemplateSpecializationDecl(
    const ClassTemplateSpecializationDecl *D) {
  VisitCXXRecordDecl(D);
  bool ShouldMangleName = Mangler->shouldMangleDeclName(D);
  if (ShouldMangleName) {
    SmallString<64> Buf;
    llvm::raw_svector_ostream StrOS(Buf);
    Mangler->mangleName(D, StrOS);
    // mangled names can get ridiculously long, so hash them to a fixed size
    OF.emitString(std::to_string(fnv64Hash(StrOS)));
  } else {
    OF.emitString("");
  }
  dumpTemplateSpecialization(D->getSpecializedTemplate(), D->getTemplateArgs());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXMethodDeclTupleSize() {
  return FunctionDeclTupleSize() + 1;
}
//@atd #define cxx_method_decl_tuple function_decl_tuple * cxx_method_decl_info
//@atd type cxx_method_decl_info = {
//@atd   ~is_virtual : bool;
//@atd   ~is_static : bool;
//@atd   ~is_constexpr : bool;
//@atd   ~cxx_ctor_initializers : cxx_ctor_initializer list;
//@atd   ~overriden_methods : decl_ref list;
//@atd } <ocaml field_prefix="xmdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXMethodDecl(const CXXMethodDecl *D) {
  VisitFunctionDecl(D);
  bool IsVirtual = D->isVirtual();
  bool IsStatic = D->isStatic();
  const CXXConstructorDecl *C = dyn_cast<CXXConstructorDecl>(D);
  bool HasCtorInitializers = C && C->init_begin() != C->init_end();
  bool IsConstexpr = D->isConstexpr();
  auto OB = D->begin_overridden_methods();
  auto OE = D->end_overridden_methods();
  ObjectScope Scope(
      OF,
      IsVirtual + IsStatic + IsConstexpr + HasCtorInitializers + (OB != OE));
  OF.emitFlag("is_virtual", IsVirtual);
  OF.emitFlag("is_static", IsStatic);
  OF.emitFlag("is_constexpr", IsConstexpr);
  if (HasCtorInitializers) {
    OF.emitTag("cxx_ctor_initializers");
    ArrayScope Scope(OF, std::distance(C->init_begin(), C->init_end()));
    for (auto I : C->inits()) {
      dumpCXXCtorInitializer(*I);
    }
  }
  if (OB != OE) {
    OF.emitTag("overriden_methods");
    ArrayScope Scope(OF, std::distance(OB, OE));
    for (; OB != OE; ++OB) {
      dumpDeclRef(**OB);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ClassTemplateDeclTupleSize() {
  return ASTExporter<ATDWriter>::RedeclarableTemplateDeclTupleSize() + 1;
}

//@atd #define class_template_decl_tuple redeclarable_template_decl_tuple * template_decl_info
//@atd type template_decl_info = {
//@atd   ~specializations : decl list;
//@atd } <ocaml field_prefix="tdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitClassTemplateDecl(
    const ClassTemplateDecl *D) {
  ASTExporter<ATDWriter>::VisitRedeclarableTemplateDecl(D);
  std::vector<const ClassTemplateSpecializationDecl *> DeclsToDump;
  if (D == D->getCanonicalDecl()) {
    // dump specializations once
    for (const auto *spec : D->specializations()) {
      switch (spec->getTemplateSpecializationKind()) {
      case TSK_Undeclared:
      case TSK_ImplicitInstantiation:
        DeclsToDump.push_back(spec);
        break;
      case TSK_ExplicitSpecialization:
      case TSK_ExplicitInstantiationDeclaration:
      case TSK_ExplicitInstantiationDefinition:
        // these specializations will be dumped elsewhere
        break;
      }
    }
  }
  bool ShouldDumpSpecializations = !DeclsToDump.empty();
  ObjectScope Scope(OF, 0 + ShouldDumpSpecializations);
  if (ShouldDumpSpecializations) {
    OF.emitTag("specializations");
    ArrayScope aScope(OF, DeclsToDump.size());
    for (const auto *spec : DeclsToDump) {
      dumpDecl(spec);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FunctionTemplateDeclTupleSize() {
  return ASTExporter<ATDWriter>::RedeclarableTemplateDeclTupleSize() + 1;
}
//@atd #define function_template_decl_tuple redeclarable_template_decl_tuple * template_decl_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFunctionTemplateDecl(
    const FunctionTemplateDecl *D) {
  ASTExporter<ATDWriter>::VisitRedeclarableTemplateDecl(D);
  std::vector<const FunctionDecl *> DeclsToDump;
  if (D == D->getCanonicalDecl()) {
    // dump specializations once
    for (const auto *spec : D->specializations()) {
      switch (spec->getTemplateSpecializationKind()) {
      case TSK_Undeclared:
      case TSK_ImplicitInstantiation:
      case TSK_ExplicitInstantiationDefinition:
      case TSK_ExplicitInstantiationDeclaration:
        DeclsToDump.push_back(spec);
        break;
      case TSK_ExplicitSpecialization:
        // these specializations will be dumped when they are defined
        break;
      }
    }
  }
  bool ShouldDumpSpecializations = !DeclsToDump.empty();
  ObjectScope Scope(OF, 0 + ShouldDumpSpecializations);
  if (ShouldDumpSpecializations) {
    OF.emitTag("specializations");
    ArrayScope aScope(OF, DeclsToDump.size());
    for (const auto *spec : DeclsToDump) {
      dumpDecl(spec);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FriendDeclTupleSize() {
  return DeclTupleSize() + 1;
}
//@atd #define friend_decl_tuple decl_tuple * friend_info
//@atd type friend_info = [ Type of type_ptr | Decl of decl ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFriendDecl(const FriendDecl *D) {
  VisitDecl(D);
  if (TypeSourceInfo *T = D->getFriendType()) {
    VariantScope Scope(OF, "Type");
    dumpQualTypeNoQuals(T->getType());
  } else {
    VariantScope Scope(OF, "Decl");
    dumpDecl(D->getFriendDecl());
  }
}

// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitTypeAliasDecl(const TypeAliasDecl *D) {
//  dumpName(D);
//  dumpQualType(D->getUnderlyingType());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitTypeAliasTemplateDecl(const
// TypeAliasTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//  dumpDecl(D->getTemplatedDecl());
//}
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitStaticAssertDecl(const StaticAssertDecl *D)
// {
//  dumpStmt(D->getAssertExpr());
//  dumpStmt(D->getMessage());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitFunctionTemplateDecl(const
// FunctionTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//  dumpDecl(D->getTemplatedDecl());
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
//        dumpDecl(*I);
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitClassTemplateDecl(const ClassTemplateDecl
// *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//
//  ClassTemplateDecl::spec_iterator I = D->spec_begin();
//  ClassTemplateDecl::spec_iterator E = D->spec_end();
//  dumpDecl(D->getTemplatedDecl());
//  for (; I != E; ++I) {
//    ClassTemplateDecl::spec_iterator Next = I;
//    ++Next;
//    switch (I->getTemplateSpecializationKind()) {
//    case TSK_Undeclared:
//    case TSK_ImplicitInstantiation:
//      if (D == D->getCanonicalDecl())
//        dumpDecl(*I);
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitClassTemplateSpecializationDecl(
//    const ClassTemplateSpecializationDecl *D) {
//  VisitCXXRecordDecl(D);
//  dumpTemplateArgumentList(D->getTemplateArgs());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitClassTemplatePartialSpecializationDecl(
//    const ClassTemplatePartialSpecializationDecl *D) {
//  VisitClassTemplateSpecializationDecl(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitClassScopeFunctionSpecializationDecl(
//    const ClassScopeFunctionSpecializationDecl *D) {
//  dumpDeclRef(D->getSpecialization());
//  if (D->hasExplicitTemplateArgs())
//    dumpTemplateArgumentListInfo(D->templateArgs());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitVarTemplateDecl(const VarTemplateDecl *D) {
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//
//  VarTemplateDecl::spec_iterator I = D->spec_begin();
//  VarTemplateDecl::spec_iterator E = D->spec_end();
//  dumpDecl(D->getTemplatedDecl());
//  for (; I != E; ++I) {
//    VarTemplateDecl::spec_iterator Next = I;
//    ++Next;
//    switch (I->getTemplateSpecializationKind()) {
//    case TSK_Undeclared:
//    case TSK_ImplicitInstantiation:
//      if (D == D->getCanonicalDecl())
//        dumpDecl(*I);
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitVarTemplateSpecializationDecl(
//    const VarTemplateSpecializationDecl *D) {
//  dumpTemplateArgumentList(D->getTemplateArgs());
//  VisitVarDecl(D);
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitVarTemplatePartialSpecializationDecl(
//    const VarTemplatePartialSpecializationDecl *D) {
//  dumpTemplateParameters(D->getTemplateParameters());
//  VisitVarTemplateSpecializationDecl(D);
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitTemplateTypeParmDecl(const
// TemplateTypeParmDecl *D) {
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitNonTypeTemplateParmDecl(const
// NonTypeTemplateParmDecl *D) {
//  dumpQualType(D->getType());
//  if (D->isParameterPack())
//    OS << " ...";
//  dumpName(D);
//  if (D->hasDefaultArgument())
//    dumpStmt(D->getDefaultArgument());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitTemplateTemplateParmDecl(
//    const TemplateTemplateParmDecl *D) {
//  if (D->isParameterPack())
//    OS << " ...";
//  dumpName(D);
//  dumpTemplateParameters(D->getTemplateParameters());
//  if (D->hasDefaultArgument())
//    dumpTemplateArgumentLoc(D->getDefaultArgument());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitUsingDecl(const UsingDecl *D) {
//  OS << ' ';
//  D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
//  OS << D->getNameAsString();
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitUnresolvedUsingTypenameDecl(
//    const UnresolvedUsingTypenameDecl *D) {
//  OS << ' ';
//  D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
//  OS << D->getNameAsString();
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitUnresolvedUsingValueDecl(const
// UnresolvedUsingValueDecl *D) {
//  OS << ' ';
//  D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
//  OS << D->getNameAsString();
//  dumpQualType(D->getType());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitUsingShadowDecl(const UsingShadowDecl *D) {
//  OS << ' ';
//  dumpDeclRef(D->getTargetDecl());
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitLinkageSpecDecl(const LinkageSpecDecl *D) {
//  switch (D->getLanguage()) {
//  case LinkageSpecDecl::lang_c: OS << " C"; break;
//  case LinkageSpecDecl::lang_cxx: OS << " C++"; break;
//  }
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::VisitAccessSpecDecl(const AccessSpecDecl *D) {
//  OS << ' ';
//  dumpAccessSpecifier(D->getAccess());
//}
//

//
////===----------------------------------------------------------------------===//
//// Obj-C Declarations
////===----------------------------------------------------------------------===//

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCIvarDeclTupleSize() {
  return FieldDeclTupleSize() + 1;
}
//@atd #define obj_c_ivar_decl_tuple field_decl_tuple * obj_c_ivar_decl_info
//@atd type obj_c_ivar_decl_info = {
//@atd   ~is_synthesize : bool;
//@atd   ~access_control <ocaml default="`None"> : obj_c_access_control;
//@atd } <ocaml field_prefix="ovdi_">
//@atd type obj_c_access_control = [ None | Private | Protected | Public | Package
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCIvarDecl(const ObjCIvarDecl *D) {
  VisitFieldDecl(D);

  bool IsSynthesize = D->getSynthesize();
  // suboptimal: access_control not taken into account accurately
  ObjectScope Scope(OF, 1 + IsSynthesize); // not covered by tests

  OF.emitFlag("is_synthesize", IsSynthesize);

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
    case ObjCIvarDecl::None:
      llvm_unreachable("unreachable");
      break;
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCMethodDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define obj_c_method_decl_tuple named_decl_tuple * obj_c_method_decl_info
//@atd type obj_c_method_decl_info = {
//@atd   ~is_instance_method : bool;
//@atd   result_type : qual_type;
//@atd   ~is_property_accessor : bool;
//@atd   ?property_decl : decl_ref option;
//@atd   ~parameters : decl list;
//@atd   ~implicit_parameters : decl list;
//@atd   ~is_variadic : bool;
//@atd   ?body : stmt option;
//@atd } <ocaml field_prefix="omdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCMethodDecl(const ObjCMethodDecl *D) {
  VisitNamedDecl(D);
  // We purposedly do not call VisitDeclContext(D).
  bool IsInstanceMethod = D->isInstanceMethod();
  bool IsPropertyAccessor = D->isPropertyAccessor();
  const ObjCPropertyDecl *PropertyDecl = nullptr;
  std::string selectorName = D->getSelector().getAsString();
  // work around bug in clang
  if (selectorName != ".cxx_construct" && selectorName != ".cxx_destruct") {
    PropertyDecl = D->findPropertyDecl();
  }
  ObjCMethodDecl::param_const_iterator I = D->param_begin(), E = D->param_end();
  bool HasParameters = I != E;
  std::vector<ImplicitParamDecl *> ImplicitParams;
  if (D->getSelfDecl()) {
    ImplicitParams.push_back(D->getSelfDecl());
  }
  if (D->getCmdDecl()) {
    ImplicitParams.push_back(D->getCmdDecl());
  }
  bool HasImplicitParameters = !ImplicitParams.empty();
  bool IsVariadic = D->isVariadic();
  const Stmt *Body = D->getBody();
  ObjectScope Scope(OF,
                    1 + IsInstanceMethod + IsPropertyAccessor +
                        (bool)PropertyDecl + HasParameters +
                        HasImplicitParameters + IsVariadic + (bool)Body);

  OF.emitFlag("is_instance_method", IsInstanceMethod);
  OF.emitTag("result_type");
  dumpQualType(D->getReturnType());
  OF.emitFlag("is_property_accessor", IsPropertyAccessor);
  if (PropertyDecl) {
    OF.emitTag("property_decl");
    dumpDeclRef(*PropertyDecl);
  }
  if (HasParameters) {
    OF.emitTag("parameters");
    ArrayScope Scope(OF, std::distance(I, E));
    for (; I != E; ++I) {
      dumpDecl(*I);
    }
  }

  if (HasImplicitParameters) {
    OF.emitTag("implicit_parameters");
    ArrayScope Scope(OF, ImplicitParams.size());
    for (const ImplicitParamDecl *P : ImplicitParams) {
      dumpDecl(P);
    }
  }

  OF.emitFlag("is_variadic", IsVariadic);

  if (Body) {
    OF.emitTag("body");
    dumpStmt(Body);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCCategoryDeclTupleSize() {
  return ObjCContainerDeclTupleSize() + 1;
}
//@atd #define obj_c_category_decl_tuple obj_c_container_decl_tuple * obj_c_category_decl_info
//@atd type obj_c_category_decl_info = {
//@atd   ?class_interface : decl_ref option;
//@atd   ?implementation : decl_ref option;
//@atd   ~protocols : decl_ref list;
//@atd } <ocaml field_prefix="odi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
  VisitObjCContainerDecl(D);

  const ObjCInterfaceDecl *CI = D->getClassInterface();
  const ObjCCategoryImplDecl *Impl = D->getImplementation();
  ObjCCategoryDecl::protocol_iterator I = D->protocol_begin(),
                                      E = D->protocol_end();
  bool HasProtocols = I != E;
  ObjectScope Scope(
      OF, 0 + (bool)CI + (bool)Impl + HasProtocols); // not covered by tests

  if (CI) {
    OF.emitTag("class_interface");
    dumpDeclRef(*CI);
  }
  if (Impl) {
    OF.emitTag("implementation");
    dumpDeclRef(*Impl);
  }
  if (HasProtocols) {
    OF.emitTag("protocols");
    ArrayScope Scope(OF, std::distance(I, E)); // not covered by tests
    for (; I != E; ++I) {
      assert(*I);
      dumpDeclRef(**I);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCCategoryImplDeclTupleSize() {
  return ASTExporter::ObjCImplDeclTupleSize() + 1;
}
//@atd #define obj_c_category_impl_decl_tuple obj_c_impl_decl_tuple * obj_c_category_impl_decl_info
//@atd type obj_c_category_impl_decl_info = {
//@atd   ?class_interface : decl_ref option;
//@atd   ?category_decl : decl_ref option;
//@atd } <ocaml field_prefix="ocidi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCCategoryImplDecl(
    const ObjCCategoryImplDecl *D) {
  ASTExporter<ATDWriter>::VisitObjCImplDecl(D);

  const ObjCInterfaceDecl *CI = D->getClassInterface();
  const ObjCCategoryDecl *CD = D->getCategoryDecl();
  ObjectScope Scope(OF, 0 + (bool)CI + (bool)CD); // not covered by tests

  if (CI) {
    OF.emitTag("class_interface");
    dumpDeclRef(*CI);
  }
  if (CD) {
    OF.emitTag("category_decl");
    dumpDeclRef(*CD);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCProtocolDeclTupleSize() {
  return ObjCContainerDeclTupleSize() + 1;
}
//@atd #define obj_c_protocol_decl_tuple obj_c_container_decl_tuple * obj_c_protocol_decl_info
//@atd type obj_c_protocol_decl_info = {
//@atd   ~protocols : decl_ref list;
//@atd } <ocaml field_prefix="opcdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
  ASTExporter<ATDWriter>::VisitObjCContainerDecl(D);

  ObjCCategoryDecl::protocol_iterator I = D->protocol_begin(),
                                      E = D->protocol_end();
  bool HasProtocols = I != E;
  ObjectScope Scope(OF, 0 + HasProtocols); // not covered by tests

  if (HasProtocols) {
    OF.emitTag("protocols");
    ArrayScope Scope(OF, std::distance(I, E)); // not covered by tests
    for (; I != E; ++I) {
      assert(*I);
      dumpDeclRef(**I);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCInterfaceDeclTupleSize() {
  return ObjCContainerDeclTupleSize() + 1;
}
//@atd #define obj_c_interface_decl_tuple obj_c_container_decl_tuple * obj_c_interface_decl_info
//@atd type obj_c_interface_decl_info = {
//@atd   ?super : decl_ref option;
//@atd   ?implementation : decl_ref option;
//@atd   ~protocols : decl_ref list;
//@atd   ~known_categories : decl_ref list;
//@atd } <ocaml field_prefix="otdi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCInterfaceDecl(
    const ObjCInterfaceDecl *D) {
  VisitObjCContainerDecl(D);

  const ObjCInterfaceDecl *SC = D->getSuperClass();
  const ObjCImplementationDecl *Impl = D->getImplementation();
  ObjCInterfaceDecl::protocol_iterator IP = D->protocol_begin(),
                                       EP = D->protocol_end();
  bool HasProtocols = IP != EP;

  ObjCInterfaceDecl::known_categories_iterator IC = D->known_categories_begin(),
                                               EC = D->known_categories_end();

  bool HasKnownCategories = IC != EC;
  ObjectScope Scope(
      OF, 0 + (bool)SC + (bool)Impl + HasProtocols + HasKnownCategories);

  if (SC) {
    OF.emitTag("super");
    dumpDeclRef(*SC);
  }
  if (Impl) {
    OF.emitTag("implementation");
    dumpDeclRef(*Impl);
  }
  if (HasProtocols) {
    OF.emitTag("protocols");
    ArrayScope Scope(OF, std::distance(IP, EP));
    for (; IP != EP; ++IP) {
      assert(*IP);
      dumpDeclRef(**IP);
    }
  }
  if (HasKnownCategories) {
    OF.emitTag("known_categories");
    ArrayScope Scope(OF, std::distance(IC, EC));
    for (; IC != EC; ++IC) {
      assert(*IC);
      dumpDeclRef(**IC);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCImplementationDeclTupleSize() {
  return ASTExporter::ObjCImplDeclTupleSize() + 1;
}
//@atd #define obj_c_implementation_decl_tuple obj_c_impl_decl_tuple * obj_c_implementation_decl_info
//@atd type obj_c_implementation_decl_info = {
//@atd   ?super : decl_ref option;
//@atd   ?class_interface : decl_ref option;
//@atd   ~ivar_initializers : cxx_ctor_initializer list;
//@atd } <ocaml field_prefix="oidi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCImplementationDecl(
    const ObjCImplementationDecl *D) {
  ASTExporter<ATDWriter>::VisitObjCImplDecl(D);

  const ObjCInterfaceDecl *SC = D->getSuperClass();
  const ObjCInterfaceDecl *CI = D->getClassInterface();
  ObjCImplementationDecl::init_const_iterator I = D->init_begin(),
                                              E = D->init_end();
  bool HasInitializers = I != E;
  ObjectScope Scope(OF, 0 + (bool)SC + (bool)CI + HasInitializers);

  if (SC) {
    OF.emitTag("super");
    dumpDeclRef(*SC);
  }
  if (CI) {
    OF.emitTag("class_interface");
    dumpDeclRef(*CI);
  }
  if (HasInitializers) {
    OF.emitTag("ivar_initializers");
    ArrayScope Scope(OF, std::distance(I, E)); // not covered by tests
    for (; I != E; ++I) {
      assert(*I);
      dumpCXXCtorInitializer(**I);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCCompatibleAliasDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define obj_c_compatible_alias_decl_tuple named_decl_tuple * obj_c_compatible_alias_decl_info
//@atd type obj_c_compatible_alias_decl_info = {
//@atd   ?class_interface : decl_ref option;
//@atd } <ocaml field_prefix="ocadi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCCompatibleAliasDecl(
    const ObjCCompatibleAliasDecl *D) {
  VisitNamedDecl(D);

  const ObjCInterfaceDecl *CI = D->getClassInterface();
  ObjectScope Scope(OF, 0 + (bool)CI); // not covered by tests

  if (CI) {
    OF.emitTag("class_interface");
    dumpDeclRef(*CI);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCPropertyDeclTupleSize() {
  return NamedDeclTupleSize() + 1;
}
//@atd #define obj_c_property_decl_tuple named_decl_tuple * obj_c_property_decl_info
//@atd type obj_c_property_decl_info = {
//@atd   qual_type : qual_type;
//@atd   ?getter_method : decl_ref option;
//@atd   ?setter_method : decl_ref option;
//@atd   ?ivar_decl : decl_ref option;
//@atd   ~property_control <ocaml default="`None"> : obj_c_property_control;
//@atd   ~property_attributes : property_attribute list
//@atd } <ocaml field_prefix="opdi_">
//@atd type obj_c_property_control = [ None | Required | Optional ]
//@atd type property_attribute = [
//@atd   Readonly
//@atd | Assign
//@atd | Readwrite
//@atd | Retain
//@atd | Copy
//@atd | Nonatomic
//@atd | Atomic
//@atd | Weak
//@atd | Strong
//@atd | Unsafe_unretained
//@atd | ExplicitGetter
//@atd | ExplicitSetter
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
  VisitNamedDecl(D);

  ObjCPropertyDecl::PropertyControl PC = D->getPropertyImplementation();
  bool HasPropertyControl = PC != ObjCPropertyDecl::None;
  ObjCPropertyDecl::PropertyAttributeKind Attrs = D->getPropertyAttributes();
  bool HasPropertyAttributes = Attrs != ObjCPropertyDecl::OBJC_PR_noattr;

  ObjCMethodDecl *Getter = D->getGetterMethodDecl();
  ObjCMethodDecl *Setter = D->getSetterMethodDecl();
  ObjCIvarDecl *Ivar = D->getPropertyIvarDecl();
  ObjectScope Scope(OF,
                    1 + (bool)Getter + (bool)Setter + (bool)Ivar +
                        HasPropertyControl +
                        HasPropertyAttributes); // not covered by tests

  OF.emitTag("qual_type");
  dumpQualType(D->getType());

  if (Getter) {
    OF.emitTag("getter_method");
    dumpDeclRef(*Getter);
  }
  if (Setter) {
    OF.emitTag("setter_method");
    dumpDeclRef(*Setter);
  }
  if (Ivar) {
    OF.emitTag("ivar_decl");
    dumpDeclRef(*Ivar);
  }

  if (HasPropertyControl) {
    OF.emitTag("property_control");
    switch (PC) {
    case ObjCPropertyDecl::Required:
      OF.emitSimpleVariant("Required");
      break;
    case ObjCPropertyDecl::Optional:
      OF.emitSimpleVariant("Optional");
      break;
    case ObjCPropertyDecl::None:
      llvm_unreachable("unreachable");
      break;
    }
  }

  if (HasPropertyAttributes) {
    OF.emitTag("property_attributes");
    bool readonly = Attrs & ObjCPropertyDecl::OBJC_PR_readonly;
    bool assign = Attrs & ObjCPropertyDecl::OBJC_PR_assign;
    bool readwrite = Attrs & ObjCPropertyDecl::OBJC_PR_readwrite;
    bool retain = Attrs & ObjCPropertyDecl::OBJC_PR_retain;
    bool copy = Attrs & ObjCPropertyDecl::OBJC_PR_copy;
    bool nonatomic = Attrs & ObjCPropertyDecl::OBJC_PR_nonatomic;
    bool atomic = Attrs & ObjCPropertyDecl::OBJC_PR_atomic;
    bool weak = Attrs & ObjCPropertyDecl::OBJC_PR_weak;
    bool strong = Attrs & ObjCPropertyDecl::OBJC_PR_strong;
    bool unsafeUnretained = Attrs & ObjCPropertyDecl::OBJC_PR_unsafe_unretained;
    bool getter = Attrs & ObjCPropertyDecl::OBJC_PR_getter;
    bool setter = Attrs & ObjCPropertyDecl::OBJC_PR_setter;
    int toEmit = readonly + assign + readwrite + retain + copy + nonatomic +
                 atomic + weak + strong + unsafeUnretained + getter + setter;
    ArrayScope Scope(OF, toEmit);
    if (readonly)
      OF.emitSimpleVariant("Readonly");
    if (assign)
      OF.emitSimpleVariant("Assign");
    if (readwrite)
      OF.emitSimpleVariant("Readwrite");
    if (retain)
      OF.emitSimpleVariant("Retain");
    if (copy)
      OF.emitSimpleVariant("Copy");
    if (nonatomic)
      OF.emitSimpleVariant("Nonatomic");
    if (atomic)
      OF.emitSimpleVariant("Atomic");
    if (weak)
      OF.emitSimpleVariant("Weak");
    if (strong)
      OF.emitSimpleVariant("Strong");
    if (unsafeUnretained)
      OF.emitSimpleVariant("Unsafe_unretained");
    if (getter) {
      OF.emitSimpleVariant("ExplicitGetter");
    }
    if (setter) {
      OF.emitSimpleVariant("ExplicitSetter");
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCPropertyImplDeclTupleSize() {
  return DeclTupleSize() + 1;
}
//@atd #define obj_c_property_impl_decl_tuple decl_tuple * obj_c_property_impl_decl_info
//@atd type obj_c_property_impl_decl_info = {
//@atd   implementation : property_implementation;
//@atd   ?property_decl : decl_ref option;
//@atd   ?ivar_decl : decl_ref option;
//@atd } <ocaml field_prefix="opidi_">
//@atd type property_implementation = [ Synthesize | Dynamic ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCPropertyImplDecl(
    const ObjCPropertyImplDecl *D) {
  VisitDecl(D);

  const ObjCPropertyDecl *PD = D->getPropertyDecl();
  const ObjCIvarDecl *ID = D->getPropertyIvarDecl();
  ObjectScope Scope(OF, 1 + (bool)PD + (bool)ID); // not covered by tests

  OF.emitTag("implementation");
  switch (D->getPropertyImplementation()) {
  case ObjCPropertyImplDecl::Synthesize:
    OF.emitSimpleVariant("Synthesize");
    break;
  case ObjCPropertyImplDecl::Dynamic:
    OF.emitSimpleVariant("Dynamic");
    break;
  }
  if (PD) {
    OF.emitTag("property_decl");
    dumpDeclRef(*PD);
  }
  if (ID) {
    OF.emitTag("ivar_decl");
    dumpDeclRef(*ID);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::BlockDeclTupleSize() {
  return DeclTupleSize() + 1;
}
//@atd #define block_decl_tuple decl_tuple * block_decl_info
//@atd type block_decl_info = {
//@atd   ~parameters : decl list;
//@atd   ~is_variadic : bool;
//@atd   ~captures_cxx_this : bool;
//@atd   ~captured_variables : block_captured_variable list;
//@atd   ?body : stmt option;
//@atd } <ocaml field_prefix="bdi_">
//@atd type block_captured_variable = {
//@atd    ~is_by_ref : bool;
//@atd    ~is_nested : bool;
//@atd    ?variable : decl_ref option;
//@atd    ?copy_expr : stmt option
//@atd } <ocaml field_prefix="bcv_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBlockDecl(const BlockDecl *D) {
  VisitDecl(D);
  // We purposedly do not call VisitDeclContext(D).

  ObjCMethodDecl::param_const_iterator PCII = D->param_begin(),
                                       PCIE = D->param_end();
  bool HasParameters = PCII != PCIE;
  bool IsVariadic = D->isVariadic();
  bool CapturesCXXThis = D->capturesCXXThis();
  BlockDecl::capture_const_iterator CII = D->capture_begin(),
                                    CIE = D->capture_end();
  bool HasCapturedVariables = CII != CIE;
  const Stmt *Body = D->getBody();
  int size = 0 + HasParameters + IsVariadic + CapturesCXXThis +
             HasCapturedVariables + (bool)Body;
  ObjectScope Scope(OF, size); // not covered by tests

  if (HasParameters) {
    OF.emitTag("parameters");
    ArrayScope Scope(OF, std::distance(PCII, PCIE));
    for (; PCII != PCIE; ++PCII) {
      dumpDecl(*PCII);
    }
  }

  OF.emitFlag("is_variadic", IsVariadic);
  OF.emitFlag("captures_cxx_this", CapturesCXXThis);

  if (HasCapturedVariables) {
    OF.emitTag("captured_variables");
    ArrayScope Scope(OF, std::distance(CII, CIE));
    for (; CII != CIE; ++CII) {
      bool IsByRef = CII->isByRef();
      bool IsNested = CII->isNested();
      bool HasVariable = CII->getVariable();
      bool HasCopyExpr = CII->hasCopyExpr();
      ObjectScope Scope(OF,
                        0 + IsByRef + IsNested + HasVariable +
                            HasCopyExpr); // not covered by tests

      OF.emitFlag("is_by_ref", IsByRef);
      OF.emitFlag("is_nested", IsNested);

      if (HasVariable) {
        OF.emitTag("variable");
        dumpDeclRef(*CII->getVariable());
      }

      if (HasCopyExpr) {
        OF.emitTag("copy_expr");
        dumpStmt(CII->getCopyExpr());
      }
    }
  }

  if (Body) {
    OF.emitTag("body");
    dumpStmt(Body);
  }
}

// main variant for declarations
//@atd type decl = [
#define DECL(DERIVED, BASE) //@atd   | DERIVED@@Decl of (@DERIVED@_decl_tuple)
#define ABSTRACT_DECL(DECL)
#include <clang/AST/DeclNodes.inc>
//@atd ] <ocaml repr="classic" validator="Clang_ast_visit.visit_decl">

//===----------------------------------------------------------------------===//
//  Stmt dumping methods.
//===----------------------------------------------------------------------===//

// Default aliases for generating variant components
// The main variant is defined at the end of section.
#define STMT(CLASS, PARENT) //@atd   #define @CLASS@_tuple @PARENT@_tuple
#define ABSTRACT_STMT(STMT) STMT
#include <clang/AST/StmtNodes.inc>
//
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpStmt(const Stmt *S) {
  if (!S) {
    // We use a fixed NullStmt node to represent null pointers
    S = NullPtrStmt;
  }
  VariantScope Scope(OF, S->getStmtClassName());
  {
    TupleScope Scope(OF, ASTExporter::tupleSizeOfStmtClass(S->getStmtClass()));
    ConstStmtVisitor<ASTExporter<ATDWriter>>::Visit(S);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::StmtTupleSize() {
  return 2;
}
//@atd #define stmt_tuple stmt_info * stmt list
//@atd type stmt_info = {
//@atd   pointer : pointer;
//@atd   source_range : source_range;
//@atd } <ocaml field_prefix="si_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitStmt(const Stmt *S) {
  {
    ObjectScope Scope(OF, 2);

    OF.emitTag("pointer");
    dumpPointer(S);
    OF.emitTag("source_range");
    dumpSourceRange(S->getSourceRange());
  }
  {
    ArrayScope Scope(OF, std::distance(S->child_begin(), S->child_end()));
    for (const Stmt *CI : S->children()) {
      dumpStmt(CI);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::DeclStmtTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define decl_stmt_tuple stmt_tuple * decl list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDeclStmt(const DeclStmt *Node) {
  VisitStmt(Node);
  ArrayScope Scope(OF, std::distance(Node->decl_begin(), Node->decl_end()));
  for (auto I : Node->decls()) {
    dumpDecl(I);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::AttributedStmtTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define attributed_stmt_tuple stmt_tuple * attribute list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAttributedStmt(const AttributedStmt *Node) {
  VisitStmt(Node);
  ArrayScope Scope(OF, Node->getAttrs().size()); // not covered by tests
  for (auto I : Node->getAttrs()) {
    dumpAttr(*I);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::LabelStmtTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define label_stmt_tuple stmt_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitLabelStmt(const LabelStmt *Node) {
  VisitStmt(Node);
  OF.emitString(Node->getName());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::GotoStmtTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define goto_stmt_tuple stmt_tuple * goto_stmt_info
//@atd type goto_stmt_info = {
//@atd   label : string;
//@atd   pointer : pointer
//@atd } <ocaml field_prefix="gsi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitGotoStmt(const GotoStmt *Node) {
  VisitStmt(Node);
  ObjectScope Scope(OF, 2); // not covered by tests
  OF.emitTag("label");
  OF.emitString(Node->getLabel()->getName());
  OF.emitTag("pointer");
  dumpPointer(Node->getLabel());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXCatchStmtTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define cxx_catch_stmt_tuple stmt_tuple * cxx_catch_stmt_info
//@atd type cxx_catch_stmt_info = {
//@atd   ?variable : decl option
//@atd } <ocaml field_prefix="xcsi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXCatchStmt(const CXXCatchStmt *Node) {
  VisitStmt(Node);

  const VarDecl *decl = Node->getExceptionDecl();
  ObjectScope Scope(OF, 0 + (bool)decl); // not covered by tests

  if (decl) {
    OF.emitTag("variable");
    dumpDecl(decl);
  }
}

////===----------------------------------------------------------------------===//
////  Expr dumping methods.
////===----------------------------------------------------------------------===//
//

template <class ATDWriter>
int ASTExporter<ATDWriter>::ExprTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define expr_tuple stmt_tuple * expr_info
//@atd type expr_info = {
//@atd   qual_type : qual_type;
//@atd   ~value_kind <ocaml default="`RValue"> : value_kind;
//@atd   ~object_kind <ocaml default="`Ordinary"> : object_kind;
//@atd } <ocaml field_prefix="ei_">
//@atd type value_kind = [ RValue | LValue | XValue ]
//@atd type object_kind = [ Ordinary | BitField | ObjCProperty | ObjCSubscript |
//@atd VectorComponent ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExpr(const Expr *Node) {
  VisitStmt(Node);

  ExprValueKind VK = Node->getValueKind();
  bool HasNonDefaultValueKind = VK != VK_RValue;
  ExprObjectKind OK = Node->getObjectKind();
  bool HasNonDefaultObjectKind = OK != OK_Ordinary;
  ObjectScope Scope(OF, 1 + HasNonDefaultValueKind + HasNonDefaultObjectKind);

  OF.emitTag("qual_type");
  dumpQualType(Node->getType());

  if (HasNonDefaultValueKind) {
    OF.emitTag("value_kind");
    switch (VK) {
    case VK_LValue:
      OF.emitSimpleVariant("LValue");
      break;
    case VK_XValue:
      OF.emitSimpleVariant("XValue");
      break;
    case VK_RValue:
      llvm_unreachable("unreachable");
      break;
    }
  }
  if (HasNonDefaultObjectKind) {
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
    case OK_Ordinary:
      llvm_unreachable("unreachable");
      break;
    }
  }
}

//@atd type cxx_base_specifier = {
//@atd   name : string;
//@atd   ~virtual : bool;
//@atd } <ocaml field_prefix="xbs_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpCXXBaseSpecifier(
    const CXXBaseSpecifier &Base) {
  bool IsVirtual = Base.isVirtual();
  ObjectScope Scope(OF, 1 + IsVirtual);

  OF.emitTag("name");
  const CXXRecordDecl *RD =
      cast<CXXRecordDecl>(Base.getType()->getAs<RecordType>()->getDecl());
  OF.emitString(RD->getName());
  OF.emitFlag("virtual", IsVirtual);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CastExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd type cast_kind = [
#define CAST_OPERATION(NAME) //@atd | NAME
#include <clang/AST/OperationKinds.def>
//@atd ]
//@atd #define cast_expr_tuple expr_tuple * cast_expr_info
//@atd type cast_expr_info = {
//@atd   cast_kind : cast_kind;
//@atd   base_path : cxx_base_specifier list;
//@atd } <ocaml field_prefix="cei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCastExpr(const CastExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF, 2);
  OF.emitTag("cast_kind");
  OF.emitSimpleVariant(Node->getCastKindName());
  OF.emitTag("base_path");
  {
    auto I = Node->path_begin(), E = Node->path_end();
    ArrayScope Scope(OF, std::distance(I, E));
    for (; I != E; ++I) {
      dumpCXXBaseSpecifier(**I);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ExplicitCastExprTupleSize() {
  return CastExprTupleSize() + 1;
}
//@atd #define explicit_cast_expr_tuple cast_expr_tuple * qual_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExplicitCastExpr(
    const ExplicitCastExpr *Node) {
  VisitCastExpr(Node);
  dumpQualType(Node->getTypeAsWritten());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::DeclRefExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define decl_ref_expr_tuple expr_tuple * decl_ref_expr_info
//@atd type decl_ref_expr_info = {
//@atd   ?decl_ref : decl_ref option;
//@atd   ?found_decl_ref : decl_ref option
//@atd } <ocaml field_prefix="drti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDeclRefExpr(const DeclRefExpr *Node) {
  VisitExpr(Node);

  const ValueDecl *D = Node->getDecl();
  const NamedDecl *FD = Node->getFoundDecl();
  bool HasFoundDeclRef = FD && D != FD;
  ObjectScope Scope(OF, 0 + (bool)D + HasFoundDeclRef);

  if (D) {
    OF.emitTag("decl_ref");
    dumpDeclRef(*D);
  }
  if (HasFoundDeclRef) {
    OF.emitTag("found_decl_ref");
    dumpDeclRef(*FD);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::OverloadExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define overload_expr_tuple expr_tuple * overload_expr_info
//@atd type overload_expr_info = {
//@atd   ~decls : decl_ref list;
//@atd   name : declaration_name;
//@atd } <ocaml field_prefix="oei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitOverloadExpr(const OverloadExpr *Node) {
  VisitExpr(Node);

  // suboptimal
  ObjectScope Scope(OF, 2); // not covered by tests

  {
    if (Node->getNumDecls() > 0) {
      OF.emitTag("decls");
      ArrayScope Scope( // not covered by tests
          OF,
          std::distance(Node->decls_begin(), Node->decls_end()));
      for (auto I : Node->decls()) {
        dumpDeclRef(*I);
      }
    }
  }
  OF.emitTag("name");
  dumpDeclarationName(Node->getName());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::UnresolvedLookupExprTupleSize() {
  return OverloadExprTupleSize() + 1;
}
//@atd #define unresolved_lookup_expr_tuple overload_expr_tuple * unresolved_lookup_expr_info
//@atd type unresolved_lookup_expr_info = {
//@atd   ~requires_ADL : bool;
//@atd   ~is_overloaded : bool;
//@atd   ?naming_class : decl_ref option;
//@atd } <ocaml field_prefix="ulei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitUnresolvedLookupExpr(
    const UnresolvedLookupExpr *Node) {
  VisitOverloadExpr(Node);

  bool RequiresADL = Node->requiresADL();
  bool IsOverloaded = Node->isOverloaded();
  bool HasNamingClass = Node->getNamingClass();
  ObjectScope Scope(
      OF,
      0 + RequiresADL + IsOverloaded + HasNamingClass); // not covered by tests

  OF.emitFlag("requires_ADL", RequiresADL);
  OF.emitFlag("is_overloaded", IsOverloaded);
  if (HasNamingClass) {
    OF.emitTag("naming_class");
    dumpDeclRef(*Node->getNamingClass());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCIvarRefExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_ivar_ref_expr_tuple expr_tuple * obj_c_ivar_ref_expr_info
//@atd type obj_c_ivar_ref_expr_info = {
//@atd   decl_ref : decl_ref;
//@atd   pointer : pointer;
//@atd   ~is_free_ivar : bool
//@atd } <ocaml field_prefix="ovrei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node) {
  VisitExpr(Node);

  bool IsFreeIvar = Node->isFreeIvar();
  ObjectScope Scope(OF, 2 + IsFreeIvar); // not covered by tests

  OF.emitTag("decl_ref");
  dumpDeclRef(*Node->getDecl());
  OF.emitTag("pointer");
  dumpPointer(Node->getDecl());
  OF.emitFlag("is_free_ivar", IsFreeIvar);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::PredefinedExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define predefined_expr_tuple expr_tuple * predefined_expr_type
//@atd type predefined_expr_type = [
//@atd | Func
//@atd | Function
//@atd | LFunction
//@atd | FuncDName
//@atd | FuncSig
//@atd | PrettyFunction
//@atd | PrettyFunctionNoVirtual
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitPredefinedExpr(const PredefinedExpr *Node) {
  VisitExpr(Node);
  switch (Node->getIdentType()) {
  case PredefinedExpr::Func:
    OF.emitSimpleVariant("Func");
    break;
  case PredefinedExpr::Function:
    OF.emitSimpleVariant("Function");
    break;
  case PredefinedExpr::LFunction:
    OF.emitSimpleVariant("LFunction");
    break;
  case PredefinedExpr::FuncDName:
    OF.emitSimpleVariant("FuncDName");
    break;
  case PredefinedExpr::FuncSig:
    OF.emitSimpleVariant("FuncSig");
    break;
  case PredefinedExpr::PrettyFunction:
    OF.emitSimpleVariant("PrettyFunction");
    break;
  case PredefinedExpr::PrettyFunctionNoVirtual:
    OF.emitSimpleVariant("PrettyFunctionNoVirtual");
    break;
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CharacterLiteralTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define character_literal_tuple expr_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCharacterLiteral(
    const CharacterLiteral *Node) {
  VisitExpr(Node);
  OF.emitInteger(Node->getValue());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::IntegerLiteralTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define integer_literal_tuple expr_tuple * integer_literal_info
//@atd type integer_literal_info = {
//@atd   ~is_signed : bool;
//@atd   bitwidth : int;
//@atd   value : string;
//@atd } <ocaml field_prefix="ili_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitIntegerLiteral(const IntegerLiteral *Node) {
  VisitExpr(Node);

  bool IsSigned = Node->getType()->isSignedIntegerType();
  ObjectScope Scope(OF, 2 + IsSigned);

  OF.emitFlag("is_signed", IsSigned);
  OF.emitTag("bitwidth");
  OF.emitInteger(Node->getValue().getBitWidth());
  OF.emitTag("value");
  OF.emitString(Node->getValue().toString(10, IsSigned));
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FloatingLiteralTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define floating_literal_tuple expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFloatingLiteral(const FloatingLiteral *Node) {
  VisitExpr(Node);
  llvm::SmallString<20> buf;
  Node->getValue().toString(buf);
  OF.emitString(buf.str());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::StringLiteralTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define string_literal_tuple expr_tuple * string list
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitStringLiteral(const StringLiteral *Str) {
  VisitExpr(Str);
  size_t n_chunks;
  if (Str->getByteLength() == 0) {
    n_chunks = 1;
  } else {
    n_chunks = 1 + ((Str->getByteLength() - 1) / Options.maxStringSize);
  }
  ArrayScope Scope(OF, n_chunks);
  for (size_t i = 0; i < n_chunks; ++i) {
    OF.emitString(Str->getBytes().substr(i * Options.maxStringSize,
                                         Options.maxStringSize));
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::UnaryOperatorTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define unary_operator_tuple expr_tuple * unary_operator_info
//@atd type unary_operator_info = {
//@atd   kind : unary_operator_kind;
//@atd   ~is_postfix : bool;
//@atd } <ocaml field_prefix="uoi_">
//@atd type unary_operator_kind = [
#define UNARY_OPERATION(NAME, SPELLING) //@atd | NAME
#include <clang/AST/OperationKinds.def>
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitUnaryOperator(const UnaryOperator *Node) {
  VisitExpr(Node);

  bool IsPostfix = Node->isPostfix();
  ObjectScope Scope(OF, 1 + IsPostfix);

  OF.emitTag("kind");
  switch (Node->getOpcode()) {
#define UNARY_OPERATION(NAME, SPELLING) \
  case UO_##NAME:                       \
    OF.emitSimpleVariant(#NAME);        \
    break;
#include <clang/AST/OperationKinds.def>
  }
  OF.emitFlag("is_postfix", IsPostfix);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::UnaryExprOrTypeTraitExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define unary_expr_or_type_trait_expr_tuple expr_tuple * unary_expr_or_type_trait_expr_info
//@atd type unary_expr_or_type_trait_expr_info = {
//@atd   kind : unary_expr_or_type_trait_kind;
//@atd   ?qual_type : qual_type option
//@atd } <ocaml field_prefix="uttei_">
//@atd type unary_expr_or_type_trait_kind = [ SizeOfWithSize of int | SizeOf | AlignOf |
//@atd   VecStep | OpenMPRequiredSimdAlign ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *Node) {
  VisitExpr(Node);

  bool HasType = Node->isArgumentType();
  ObjectScope Scope(OF, 1 + HasType); // not covered by tests

  OF.emitTag("kind");
  switch (Node->getKind()) {
  case UETT_SizeOf: {
    const Type *ArgType = Node->getTypeOfArgument().getTypePtr();
    if (hasMeaningfulTypeInfo(ArgType)) {
      VariantScope Scope(OF, "SizeOfWithSize");
      OF.emitInteger(Context.getTypeInfo(ArgType).Width / 8);
    } else {
      OF.emitSimpleVariant("SizeOf");
    }
    break;
  }
  case UETT_AlignOf:
    OF.emitSimpleVariant("AlignOf");
    break;
  case UETT_VecStep:
    OF.emitSimpleVariant("VecStep");
    break;
  case UETT_OpenMPRequiredSimdAlign:
    OF.emitSimpleVariant("OpenMPRequiredSimdAlign");
    break;
  }
  if (HasType) {
    OF.emitTag("qual_type");
    dumpQualType(Node->getArgumentType());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::MemberExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define member_expr_tuple expr_tuple * member_expr_info
//@atd type member_expr_info = {
//@atd   ~is_arrow : bool;
//@atd   ~performs_virtual_dispatch : bool;
//@atd   name : named_decl_info;
//@atd   decl_ref : decl_ref
//@atd } <ocaml field_prefix="mei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitMemberExpr(const MemberExpr *Node) {
  VisitExpr(Node);

  bool IsArrow = Node->isArrow();
  LangOptions LO;
  // ignore real lang options - it will get it wrong when compiling
  // with -fapple-kext flag
  bool PerformsVirtualDispatch = Node->performsVirtualDispatch(LO);
  ObjectScope Scope(OF, 2 + IsArrow + PerformsVirtualDispatch);

  OF.emitFlag("is_arrow", IsArrow);
  OF.emitFlag("performs_virtual_dispatch", PerformsVirtualDispatch);
  OF.emitTag("name");
  ValueDecl *memberDecl = Node->getMemberDecl();
  dumpName(*memberDecl);
  OF.emitTag("decl_ref");
  dumpDeclRef(*memberDecl);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ExtVectorElementExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define ext_vector_element_tuple expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExtVectorElementExpr(
    const ExtVectorElementExpr *Node) {
  VisitExpr(Node);
  OF.emitString(Node->getAccessor().getNameStart());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::BinaryOperatorTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define binary_operator_tuple expr_tuple * binary_operator_info
//@atd type binary_operator_info = {
//@atd   kind : binary_operator_kind
//@atd } <ocaml field_prefix="boi_">
//@atd type binary_operator_kind = [
#define BINARY_OPERATION(NAME, SPELLING) //@atd | NAME
#include <clang/AST/OperationKinds.def>
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBinaryOperator(const BinaryOperator *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF, 1);
  OF.emitTag("kind");
  switch (Node->getOpcode()) {
#define BINARY_OPERATION(NAME, SPELLING) \
  case BO_##NAME:                        \
    OF.emitSimpleVariant(#NAME);         \
    break;
#include <clang/AST/OperationKinds.def>
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CompoundAssignOperatorTupleSize() {
  return BinaryOperatorTupleSize() + 1;
}
//@atd #define compound_assign_operator_tuple binary_operator_tuple * compound_assign_operator_info
//@atd type compound_assign_operator_info = {
//@atd   lhs_type : qual_type;
//@atd   result_type : qual_type;
//@atd } <ocaml field_prefix="caoi_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCompoundAssignOperator(
    const CompoundAssignOperator *Node) {
  VisitBinaryOperator(Node);
  ObjectScope Scope(OF, 2); // not covered by tests
  OF.emitTag("lhs_type");
  dumpQualType(Node->getComputationLHSType());
  OF.emitTag("result_type");
  dumpQualType(Node->getComputationResultType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::BlockExprTupleSize() {
  return ExprTupleSize() + DeclTupleSize();
}
//@atd #define block_expr_tuple expr_tuple * decl
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBlockExpr(const BlockExpr *Node) {
  VisitExpr(Node);
  dumpDecl(Node->getBlockDecl());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::OpaqueValueExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define opaque_value_expr_tuple expr_tuple * opaque_value_expr_info
//@atd type  opaque_value_expr_info = {
//@atd   ?source_expr : stmt option;
//@atd } <ocaml field_prefix="ovei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitOpaqueValueExpr(const OpaqueValueExpr *Node) {
  VisitExpr(Node);

  const Expr *Source = Node->getSourceExpr();
  ObjectScope Scope(OF, 0 + (bool)Source); // not covered by tests

  if (Source) {
    OF.emitTag("source_expr");
    dumpStmt(Source);
  }
}

// GNU extensions.

template <class ATDWriter>
int ASTExporter<ATDWriter>::AddrLabelExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define addr_label_expr_tuple expr_tuple * addr_label_expr_info
//@atd type addr_label_expr_info = {
//@atd   label : string;
//@atd   pointer : pointer;
//@atd } <ocaml field_prefix="alei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAddrLabelExpr(const AddrLabelExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF, 2); // not covered by tests
  OF.emitTag("label");
  OF.emitString(Node->getLabel()->getName());
  OF.emitTag("pointer");
  dumpPointer(Node->getLabel());
}

////===----------------------------------------------------------------------===//
//// C++ Expressions
////===----------------------------------------------------------------------===//

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXNamedCastExprTupleSize() {
  return ExplicitCastExprTupleSize() + 1;
}
//@atd #define cxx_named_cast_expr_tuple explicit_cast_expr_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXNamedCastExpr(
    const CXXNamedCastExpr *Node) {
  VisitExplicitCastExpr(Node);
  OF.emitString(Node->getCastName());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXBoolLiteralExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_bool_literal_expr_tuple expr_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXBoolLiteralExpr(
    const CXXBoolLiteralExpr *Node) {
  VisitExpr(Node);
  OF.emitInteger(Node->getValue());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXConstructExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_construct_expr_tuple expr_tuple * cxx_construct_expr_info
//@atd type cxx_construct_expr_info = {
//@atd   decl_ref : decl_ref;
//@atd   ~is_elidable : bool;
//@atd   ~requires_zero_initialization : bool;
//@atd } <ocaml field_prefix="xcei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXConstructExpr(
    const CXXConstructExpr *Node) {
  VisitExpr(Node);

  bool IsElidable = Node->isElidable();
  bool RequiresZeroInitialization = Node->requiresZeroInitialization();
  ObjectScope Scope(OF, 1 + IsElidable + RequiresZeroInitialization);

  OF.emitTag("decl_ref");
  dumpDeclRef(*Node->getConstructor());
  OF.emitFlag("is_elidable", IsElidable);
  OF.emitFlag("requires_zero_initialization", RequiresZeroInitialization);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXBindTemporaryExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_bind_temporary_expr_tuple expr_tuple * cxx_bind_temporary_expr_info
//@atd type cxx_bind_temporary_expr_info = {
//@atd   cxx_temporary : cxx_temporary;
//@atd } <ocaml field_prefix="xbtei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXBindTemporaryExpr(
    const CXXBindTemporaryExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF, 1);
  OF.emitTag("cxx_temporary");
  dumpCXXTemporary(Node->getTemporary());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::MaterializeTemporaryExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define materialize_temporary_expr_tuple expr_tuple * materialize_temporary_expr_info
//@atd type materialize_temporary_expr_info = {
//@atd   ?decl_ref : decl_ref option;
//@atd } <ocaml field_prefix="mtei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *Node) {
  VisitExpr(Node);

  const ValueDecl *VD = Node->getExtendingDecl();
  ObjectScope Scope(OF, 0 + (bool)VD);
  if (VD) {
    OF.emitTag("decl_ref");
    dumpDeclRef(*VD);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ExprWithCleanupsTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define expr_with_cleanups_tuple expr_tuple * expr_with_cleanups_info
//@atd type expr_with_cleanups_info = {
//@atd  ~decl_refs : decl_ref list;
//@atd } <ocaml field_prefix="ewci_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitExprWithCleanups(
    const ExprWithCleanups *Node) {
  VisitExpr(Node);

  bool HasDeclRefs = Node->getNumObjects() > 0;
  ObjectScope Scope(OF, 0 + HasDeclRefs);

  if (HasDeclRefs) {
    OF.emitTag("decl_refs");
    ArrayScope Scope(OF, Node->getNumObjects());
    for (unsigned i = 0, e = Node->getNumObjects(); i != e; ++i)
      dumpDeclRef(*Node->getObject(i));
  }
}

//@atd type cxx_temporary = pointer
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpCXXTemporary(const CXXTemporary *Temporary) {
  dumpPointer(Temporary);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::LambdaExprTupleSize() {
  return ExprTupleSize() + DeclTupleSize();
}

//@atd #define lambda_expr_tuple expr_tuple * lambda_expr_info
//@atd type lambda_expr_info = {
//@atd   lambda_decl: decl;
//@atd } <ocaml field_prefix="lei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitLambdaExpr(const LambdaExpr *Node) {
  VisitExpr(Node);

  ObjectScope Scope(OF, 1);
  OF.emitTag("lambda_decl");
  dumpDecl(Node->getLambdaClass());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXNewExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_new_expr_tuple expr_tuple * cxx_new_expr_info
//@atd type cxx_new_expr_info = {
//@atd   ~is_array : bool;
//@atd   ?array_size_expr : pointer option;
//@atd   ?initializer_expr : pointer option;
//@atd } <ocaml field_prefix="xnei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXNewExpr(const CXXNewExpr *Node) {
  VisitExpr(Node);

  bool IsArray = Node->isArray();
  bool HasArraySize = Node->getArraySize();
  bool HasInitializer = Node->hasInitializer();
  ObjectScope Scope(OF, 0 + IsArray + HasArraySize + HasInitializer);

  //  ?should_null_check : bool;
  // OF.emitFlag("should_null_check", Node->shouldNullCheckAllocation());
  OF.emitFlag("is_array", IsArray);
  if (HasArraySize) {
    OF.emitTag("array_size_expr");
    dumpPointer(Node->getArraySize());
  }
  if (HasInitializer) {
    OF.emitTag("initializer_expr");
    dumpPointer(Node->getInitializer());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXDeleteExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_delete_expr_tuple expr_tuple * cxx_delete_expr_info
//@atd type cxx_delete_expr_info = {
//@atd   ~is_array : bool;
//@atd   destroyed_type : qual_type;
//@atd } <ocaml field_prefix="xdei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXDeleteExpr(const CXXDeleteExpr *Node) {
  VisitExpr(Node);

  bool IsArray = Node->isArrayForm();
  ObjectScope Scope(OF, 1 + IsArray);

  OF.emitFlag("is_array", IsArray);

  OF.emitTag("destroyed_type");
  dumpQualType(Node->getDestroyedType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXDefaultArgExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_default_arg_expr_tuple expr_tuple * cxx_default_expr_info
//@atd type cxx_default_expr_info = {
//@atd   ?init_expr : stmt option;
//@atd } <ocaml field_prefix="xdaei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXDefaultArgExpr(
    const CXXDefaultArgExpr *Node) {
  VisitExpr(Node);

  const Expr *InitExpr = Node->getExpr();
  ObjectScope Scope(OF, 0 + (bool)InitExpr);
  if (InitExpr) {
    OF.emitTag("init_expr");
    dumpStmt(InitExpr);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXDefaultInitExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_default_init_expr_tuple expr_tuple * cxx_default_expr_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXDefaultInitExpr(
    const CXXDefaultInitExpr *Node) {
  VisitExpr(Node);

  const Expr *InitExpr = Node->getExpr();
  ObjectScope Scope(OF, 0 + (bool)InitExpr);
  if (InitExpr) {
    OF.emitTag("init_expr");
    dumpStmt(InitExpr);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TypeTraitExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define type_trait_expr_tuple expr_tuple * type_trait_info
//@atd type type_trait_info = {
//@atd   ~value : bool;
//@atd } <ocaml field_prefix="xtti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTypeTraitExpr(const TypeTraitExpr *Node) {
  VisitExpr(Node);
  // FIXME: don't dump false when value is dependent
  bool value = Node->isValueDependent() ? false : Node->getValue();
  ObjectScope Scope(OF, 0 + value);
  OF.emitFlag("value", value);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CXXNoexceptExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define cxx_noexcept_expr_tuple expr_tuple * cxx_noexcept_expr_info
//@atd type cxx_noexcept_expr_info = {
//@atd   ~value : bool;
//@atd } <ocaml field_prefix="xnee_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitCXXNoexceptExpr(const CXXNoexceptExpr *Node) {
  VisitExpr(Node);
  bool value = Node->getValue();
  ObjectScope Scope(OF, 0 + value);
  OF.emitFlag("value", value);
}

////===----------------------------------------------------------------------===//
//// Obj-C Expressions
////===----------------------------------------------------------------------===//

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCMessageExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_message_expr_tuple expr_tuple * obj_c_message_expr_info
//@atd type obj_c_message_expr_info = {
//@atd   selector : string;
//@atd   ~is_definition_found : bool;
//@atd   ?decl_pointer : pointer option;
//@atd   ~receiver_kind <ocaml default="`Instance"> : receiver_kind
//@atd } <ocaml field_prefix="omei_">
//@atd type receiver_kind = [ Instance | Class of qual_type | SuperInstance |
//@atd SuperClass ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCMessageExpr(const ObjCMessageExpr *Node) {
  VisitExpr(Node);

  bool IsDefinitionFound = false;
  // Do not rely on Node->getMethodDecl() - it might be wrong if
  // selector doesn't type check (ie. method of subclass is called)
  const ObjCInterfaceDecl *receiver = Node->getReceiverInterface();
  const Selector selector = Node->getSelector();
  const ObjCMethodDecl *m_decl = NULL;
  if (receiver) {
    bool IsInst = Node->isInstanceMessage();
    m_decl = receiver->lookupPrivateMethod(selector, IsInst);
    // Look for definition first. It's possible that class redefines it without
    // redeclaring. It needs to be defined in same translation unit to work.
    if (m_decl) {
      IsDefinitionFound = true;
    } else {
      // As a fallback look through method declarations in the interface.
      // It's not very reliable (subclass might have redefined it)
      // but it's better than nothing
      IsDefinitionFound = false;
      m_decl = receiver->lookupMethod(selector, IsInst);
    }
  }
  ObjCMessageExpr::ReceiverKind RK = Node->getReceiverKind();
  bool HasNonDefaultReceiverKind = RK != ObjCMessageExpr::Instance;
  ObjectScope Scope(
      OF, 1 + IsDefinitionFound + (bool)m_decl + HasNonDefaultReceiverKind);

  OF.emitTag("selector");
  OF.emitString(selector.getAsString());

  if (m_decl) {
    OF.emitFlag("is_definition_found", IsDefinitionFound);
    OF.emitTag("decl_pointer");
    dumpPointer(m_decl);
  }

  if (HasNonDefaultReceiverKind) {
    OF.emitTag("receiver_kind");
    switch (RK) {
    case ObjCMessageExpr::Class: {
      VariantScope Scope(OF, "Class");
      dumpQualType(Node->getClassReceiver());
    } break;
    case ObjCMessageExpr::SuperInstance:
      OF.emitSimpleVariant("SuperInstance");
      break;
    case ObjCMessageExpr::SuperClass:
      OF.emitSimpleVariant("SuperClass");
      break;
    case ObjCMessageExpr::Instance:
      llvm_unreachable("unreachable");
      break;
    }
  }
}

//@atd type selector = string
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpSelector(const Selector sel) {
  OF.emitString(sel.getAsString());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCBoxedExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_boxed_expr_tuple expr_tuple * objc_boxed_expr_info
//@atd type objc_boxed_expr_info = {
//@atd   ?boxing_method : selector option;
//@atd }  <ocaml field_prefix="obei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCBoxedExpr(const ObjCBoxedExpr *Node) {
  VisitExpr(Node);
  ObjCMethodDecl *boxingMethod = Node->getBoxingMethod();
  ObjectScope Scope(OF, 0 + (bool)boxingMethod);
  if (boxingMethod) {
    OF.emitTag("boxing_method");
    dumpSelector(boxingMethod->getSelector());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCAtCatchStmtTupleSize() {
  return StmtTupleSize() + 1;
}
//@atd #define obj_c_at_catch_stmt_tuple stmt_tuple * obj_c_message_expr_kind
//@atd type obj_c_message_expr_kind = [
//@atd | CatchParam of decl
//@atd | CatchAll
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node) {
  VisitStmt(Node);
  if (const VarDecl *CatchParam = Node->getCatchParamDecl()) {
    VariantScope Scope(OF, "CatchParam");
    dumpDecl(CatchParam);
  } else {
    OF.emitSimpleVariant("CatchAll");
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCEncodeExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_encode_expr_tuple expr_tuple * objc_encode_expr_info
//@atd type objc_encode_expr_info = {
//@atd   qual_type : qual_type;
//@atd   raw : string;
//@atd } <ocaml field_prefix="oeei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCEncodeExpr(const ObjCEncodeExpr *Node) {
  VisitExpr(Node);
  ObjectScope Scope(OF, 2);
  OF.emitTag("qual_type");
  dumpQualType(Node->getEncodedType());
  OF.emitTag("raw");
  OF.emitString(Node->getEncodedType().getAsString());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCSelectorExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_selector_expr_tuple expr_tuple * selector
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCSelectorExpr(
    const ObjCSelectorExpr *Node) {
  VisitExpr(Node);
  dumpSelector(Node->getSelector());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCProtocolExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_protocol_expr_tuple expr_tuple * decl_ref
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCProtocolExpr(
    const ObjCProtocolExpr *Node) {
  VisitExpr(Node);
  dumpDeclRef(*Node->getProtocol());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCPropertyRefExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_property_ref_expr_tuple expr_tuple * obj_c_property_ref_expr_info
//@atd type obj_c_property_ref_expr_info = {
//@atd   kind : property_ref_kind;
//@atd   ~is_super_receiver : bool;
//@atd   ~is_messaging_getter : bool;
//@atd   ~is_messaging_setter : bool;
//@atd } <ocaml field_prefix="oprei_">
//@atd type property_ref_kind = [
//@atd | MethodRef of obj_c_method_ref_info
//@atd | PropertyRef of decl_ref
//@atd ]
//@atd type obj_c_method_ref_info = {
//@atd   ?getter : selector option;
//@atd   ?setter : selector option
//@atd } <ocaml field_prefix="mri_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCPropertyRefExpr(
    const ObjCPropertyRefExpr *Node) {
  VisitExpr(Node);

  bool IsSuperReceiver = Node->isSuperReceiver();
  bool IsMessagingGetter = Node->isMessagingGetter();
  bool IsMessagingSetter = Node->isMessagingSetter();
  ObjectScope Scope(OF,
                    1 + IsSuperReceiver + IsMessagingGetter +
                        IsMessagingSetter); // not covered by tests

  OF.emitTag("kind");
  if (Node->isImplicitProperty()) {
    VariantScope Scope(OF, "MethodRef");
    {
      bool HasImplicitPropertyGetter = Node->getImplicitPropertyGetter();
      bool HasImplicitPropertySetter = Node->getImplicitPropertySetter();
      ObjectScope Scope(
          OF, 0 + HasImplicitPropertyGetter + HasImplicitPropertySetter);

      if (HasImplicitPropertyGetter) {
        OF.emitTag("getter");
        dumpSelector(Node->getImplicitPropertyGetter()->getSelector());
      }
      if (HasImplicitPropertySetter) {
        OF.emitTag("setter");
        dumpSelector(Node->getImplicitPropertySetter()->getSelector());
      }
    }
  } else {
    VariantScope Scope(OF, "PropertyRef");
    dumpDeclRef(*Node->getExplicitProperty());
  }
  OF.emitFlag("is_super_receiver", IsSuperReceiver);
  OF.emitFlag("is_messaging_getter", IsMessagingGetter);
  OF.emitFlag("is_messaging_setter", IsMessagingSetter);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCSubscriptRefExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_subscript_ref_expr_tuple expr_tuple * obj_c_subscript_ref_expr_info
//@atd type obj_c_subscript_ref_expr_info = {
//@atd   kind : obj_c_subscript_kind;
//@atd   ?getter : selector option;
//@atd   ?setter : selector option
//@atd } <ocaml field_prefix="osrei_">
//@atd type obj_c_subscript_kind = [ ArraySubscript | DictionarySubscript ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCSubscriptRefExpr(
    const ObjCSubscriptRefExpr *Node) {
  VisitExpr(Node);

  bool HasGetter = Node->getAtIndexMethodDecl();
  bool HasSetter = Node->setAtIndexMethodDecl();
  ObjectScope Scope(OF, 1 + HasGetter + HasSetter); // not covered by tests

  OF.emitTag("kind");
  if (Node->isArraySubscriptRefExpr()) {
    OF.emitSimpleVariant("ArraySubscript");
  } else {
    OF.emitSimpleVariant("DictionarySubscript");
  }
  if (HasGetter) {
    OF.emitTag("getter");
    dumpSelector(Node->getAtIndexMethodDecl()->getSelector());
  }
  if (HasSetter) {
    OF.emitTag("setter");
    dumpSelector(Node->setAtIndexMethodDecl()->getSelector());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCBoolLiteralExprTupleSize() {
  return ExprTupleSize() + 1;
}
//@atd #define obj_c_bool_literal_expr_tuple expr_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCBoolLiteralExpr(
    const ObjCBoolLiteralExpr *Node) {
  VisitExpr(Node);
  OF.emitInteger(Node->getValue());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCAvailabilityCheckExprTupleSize() {
  return ExprTupleSize() + 1;
}

//@atd #define obj_c_availability_check_expr_tuple expr_tuple * obj_c_availability_check_expr_info
//@atd type obj_c_availability_check_expr_info = {
//@atd   ?version : string option;
//@atd } <ocaml field_prefix="oacei_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCAvailabilityCheckExpr(
    const ObjCAvailabilityCheckExpr *Expr) {
  VisitExpr(Expr);
  bool HasVersion = Expr->hasVersion();
  ObjectScope Scope(OF, HasVersion);
  if (HasVersion) {
    OF.emitTag("version");
    ObjCAvailabilityCheckExpr *E = (ObjCAvailabilityCheckExpr *)Expr;
    OF.emitString(E->getVersion().getAsString());
  }
}

// Main variant for statements
//@atd type stmt = [
#define STMT(CLASS, PARENT) //@atd   | CLASS of (@CLASS@_tuple)
#define ABSTRACT_STMT(STMT)
#include <clang/AST/StmtNodes.inc>
//@atd ] <ocaml repr="classic" validator="Clang_ast_visit.visit_stmt">

//===----------------------------------------------------------------------===//
// Comments
//===----------------------------------------------------------------------===//

template <class ATDWriter>
const char *ASTExporter<ATDWriter>::getCommandName(unsigned CommandID) {
  return Context.getCommentCommandTraits().getCommandInfo(CommandID)->Name;
}

template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpFullComment(const FullComment *C) {
  FC = C;
  dumpComment(C);
  FC = 0;
}

#define COMMENT(CLASS, PARENT) //@atd #define @CLASS@_tuple @PARENT@_tuple
#define ABSTRACT_COMMENT(COMMENT) COMMENT
#include <clang/AST/CommentNodes.inc>
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpComment(const Comment *C) {
  if (!C) {
    // We use a fixed NoComment node to represent null pointers
    C = NullPtrComment;
  }
  VariantScope Scope(OF, std::string(C->getCommentKindName()));
  {
    TupleScope Scope(OF,
                     ASTExporter::tupleSizeOfCommentKind(C->getCommentKind()));
    ConstCommentVisitor<ASTExporter<ATDWriter>>::visit(C);
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::CommentTupleSize() {
  return 2;
}
//@atd #define comment_tuple comment_info * comment list
//@atd type comment_info = {
//@atd   parent_pointer : pointer;
//@atd   source_range : source_range;
//@atd } <ocaml field_prefix="ci_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::visitComment(const Comment *C) {
  {
    ObjectScope ObjComment(OF, 2); // not covered by tests
    OF.emitTag("parent_pointer");
    dumpPointer(C);
    OF.emitTag("source_range");
    dumpSourceRange(C->getSourceRange());
  }
  {
    Comment::child_iterator I = C->child_begin(), E = C->child_end();
    ArrayScope Scope(OF, std::distance(I, E));
    for (; I != E; ++I) {
      dumpComment(*I);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TextCommentTupleSize() {
  return CommentTupleSize() + 1;
}
//@atd #define text_comment_tuple comment_tuple * string
template <class ATDWriter>
void ASTExporter<ATDWriter>::visitTextComment(const TextComment *C) {
  visitComment(C);
  OF.emitString(C->getText());
}

// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitInlineCommandComment(const
// InlineCommandComment *C) {
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitHTMLStartTagComment(const
// HTMLStartTagComment *C) {
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitHTMLEndTagComment(const HTMLEndTagComment
// *C) {
//  OS << " Name=\"" << C->getTagName() << "\"";
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitBlockCommandComment(const
// BlockCommandComment *C) {
//  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
//  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
//    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitParamCommandComment(const
// ParamCommandComment *C) {
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
//  if (C->isParamIndexValid() && !C->isVarArgParam())
//    OS << " ParamIndex=" << C->getParamIndex();
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitTParamCommandComment(const
// TParamCommandComment *C) {
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
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitVerbatimBlockComment(const
// VerbatimBlockComment *C) {
//  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\""
//        " CloseName=\"" << C->getCloseName() << "\"";
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitVerbatimBlockLineComment(
//    const VerbatimBlockLineComment *C) {
//  OS << " Text=\"" << C->getText() << "\"";
//}
//
// template <class ATDWriter>
// void ASTExporter<ATDWriter>::visitVerbatimLineComment(const
// VerbatimLineComment *C) {
//  OS << " Text=\"" << C->getText() << "\"";
//}

//@atd type comment = [
#define COMMENT(CLASS, PARENT) //@atd   | CLASS of (@CLASS@_tuple)
#define ABSTRACT_COMMENT(COMMENT)
#include <clang/AST/CommentNodes.inc>
//@atd ] <ocaml repr="classic">

#define TYPE(DERIVED, BASE) //@atd #define @DERIVED@_type_tuple @BASE@_tuple
#define ABSTRACT_TYPE(DERIVED, BASE) TYPE(DERIVED, BASE)
TYPE(None, Type)
#include <clang/AST/TypeNodes.def>
#undef TYPE
#undef ABSTRACT_TYPE

template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpType(const Type *T) {

  std::string typeClassName = T ? T->getTypeClassName() : "None";
  VariantScope Scope(OF, typeClassName + "Type");
  {
    if (T) {
      // TypeVisitor assumes T is non-null
      TupleScope Scope(OF,
                       ASTExporter::tupleSizeOfTypeClass(T->getTypeClass()));
      TypeVisitor<ASTExporter<ATDWriter>>::Visit(T);
    } else {
      TupleScope Scope(OF, 1);
      VisitType(nullptr);
    }
  }
}

//@atd type type_ptr = int wrap <ocaml module="Clang_ast_types.TypePtr">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpPointerToType(const Type *T) {
  dumpPointer(T);
}

template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpQualTypeNoQuals(const QualType &qt) {
  const Type *T = qt.getTypePtrOrNull();
  dumpPointerToType(T);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TypeTupleSize() {
  return 1;
}
template <class ATDWriter>
int ASTExporter<ATDWriter>::TypeWithChildInfoTupleSize() {
  return 2;
}
//@atd #define type_tuple type_info
//@atd type type_info = {
//@atd   pointer : pointer;
//@atd   ?desugared_type : type_ptr option;
//@atd } <ocaml field_prefix="ti_">
//@atd #define type_with_child_info type_info * qual_type
//@atd #define qual_type_with_child_info type_info * qual_type
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitType(const Type *T) {
  // NOTE: T can (and will) be null here!!

  bool HasDesugaredType = T && T->getUnqualifiedDesugaredType() != T;
  ObjectScope Scope(OF, 1 + HasDesugaredType);

  OF.emitTag("pointer");
  dumpPointer(T);

  if (HasDesugaredType) {
    OF.emitTag("desugared_type");
    dumpPointerToType(T->getUnqualifiedDesugaredType());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::AdjustedTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define adjusted_type_tuple type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAdjustedType(const AdjustedType *T) {
  VisitType(T);
  dumpQualType(T->getAdjustedType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ArrayTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define array_type_tuple type_tuple * array_type_info
//@atd type array_type_info = {
//@atd   element_type : qual_type;
//@atd   ?stride : int option;
//@atd } <ocaml field_prefix="arti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitArrayType(const ArrayType *T) {
  VisitType(T);
  QualType EltT = T->getElementType();
  bool HasStride = hasMeaningfulTypeInfo(EltT.getTypePtr());
  ObjectScope Scope(OF, 1 + HasStride);
  OF.emitTag("element_type");
  dumpQualType(EltT);
  if (HasStride) {
    OF.emitTag("stride");
    OF.emitInteger(Context.getTypeInfo(EltT).Width / 8);
  };
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ConstantArrayTypeTupleSize() {
  return ArrayTypeTupleSize() + 1;
}
//@atd #define constant_array_type_tuple array_type_tuple * int
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitConstantArrayType(
    const ConstantArrayType *T) {
  VisitArrayType(T);
  OF.emitInteger(T->getSize().getLimitedValue());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::VariableArrayTypeTupleSize() {
  return ArrayTypeTupleSize() + 1;
}
//@atd #define variable_array_type_tuple array_type_tuple * pointer
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitVariableArrayType(
    const VariableArrayType *T) {
  VisitArrayType(T);
  dumpPointer(T->getSizeExpr());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::AtomicTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define atomic_type_tuple type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAtomicType(const AtomicType *T) {
  VisitType(T);
  dumpQualType(T->getValueType());
}

//@atd type type_attribute_kind = [
//@atd   | AddressSpace
//@atd   | Regparm
//@atd   | VectorSize
//@atd   | NeonVectorSize
//@atd   | NeonPolyVectorSize
//@atd   | ObjcGc
//@atd   | ObjcOwnership
//@atd   | Pcs
//@atd   | PcsVfp
//@atd   | Noreturn
//@atd   | Cdecl
//@atd   | Fastcall
//@atd   | Stdcall
//@atd   | Thiscall
//@atd   | Pascal
//@atd   | Vectorcall
//@atd   | Inteloclbicc
//@atd   | MsAbi
//@atd   | SysvAbi
//@atd   | Ptr32
//@atd   | Ptr64
//@atd   | Sptr
//@atd   | Uptr
//@atd   | Nonnull
//@atd   | Nullable
//@atd   | NullUnspecified
//@atd   | ObjcKindof
//@atd   | ObjcInsertUnsafeUnretained
//@atd   | Swiftcall
//@atd   | PreserveMost
//@atd   | PreserveAll
//@atd   | Regcall
//@atd ]

template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpTypeAttr(AttributedType::Kind kind) {
  switch (kind) {
  case AttributedType::attr_address_space:
    OF.emitSimpleVariant("AddressSpace");
    break;
  case AttributedType::attr_regparm:
    OF.emitSimpleVariant("Regparm");
    break;
  case AttributedType::attr_vector_size:
    OF.emitSimpleVariant("VectorSize");
    break;
  case AttributedType::attr_neon_vector_type:
    OF.emitSimpleVariant("NeonVectorSize");
    break;
  case AttributedType::attr_neon_polyvector_type:
    OF.emitSimpleVariant("NeonPolyVectorSize");
    break;
  case AttributedType::attr_objc_gc:
    OF.emitSimpleVariant("ObjcGc");
    break;
  case AttributedType::attr_objc_ownership:
    OF.emitSimpleVariant("ObjcOwnership");
    break;
  case AttributedType::attr_pcs:
    OF.emitSimpleVariant("Pcs");
    break;
  case AttributedType::attr_pcs_vfp:
    OF.emitSimpleVariant("PcsVfp");
    break;
  case AttributedType::attr_noreturn:
    OF.emitSimpleVariant("Noreturn");
    break;
  case AttributedType::attr_cdecl:
    OF.emitSimpleVariant("Cdecl");
    break;
  case AttributedType::attr_fastcall:
    OF.emitSimpleVariant("Fastcall");
    break;
  case AttributedType::attr_stdcall:
    OF.emitSimpleVariant("Stdcall");
    break;
  case AttributedType::attr_thiscall:
    OF.emitSimpleVariant("Thiscall");
    break;
  case AttributedType::attr_pascal:
    OF.emitSimpleVariant("Pascal");
    break;
  case AttributedType::attr_vectorcall:
    OF.emitSimpleVariant("Vectorcall");
    break;
  case AttributedType::attr_inteloclbicc:
    OF.emitSimpleVariant("Inteloclbicc");
    break;
  case AttributedType::attr_ms_abi:
    OF.emitSimpleVariant("MsAbi");
    break;
  case AttributedType::attr_sysv_abi:
    OF.emitSimpleVariant("SysvAbi");
    break;
  case AttributedType::attr_ptr32:
    OF.emitSimpleVariant("Ptr32");
    break;
  case AttributedType::attr_ptr64:
    OF.emitSimpleVariant("Ptr64");
    break;
  case AttributedType::attr_sptr:
    OF.emitSimpleVariant("Sptr");
    break;
  case AttributedType::attr_uptr:
    OF.emitSimpleVariant("Uptr");
    break;
  case AttributedType::attr_nonnull:
    OF.emitSimpleVariant("Nonnull");
    break;
  case AttributedType::attr_nullable:
    OF.emitSimpleVariant("Nullable");
    break;
  case AttributedType::attr_null_unspecified:
    OF.emitSimpleVariant("NullUnspecified");
    break;
  case AttributedType::attr_objc_kindof:
    OF.emitSimpleVariant("ObjcKindof");
    break;
  case AttributedType::attr_objc_inert_unsafe_unretained:
    OF.emitSimpleVariant("ObjcInsertUnsafeUnretained");
    break;
  case AttributedType::attr_swiftcall:
    OF.emitSimpleVariant("Swiftcall");
    break;
  case AttributedType::attr_preserve_most:
    OF.emitSimpleVariant("PreserveMost");
    break;
  case AttributedType::attr_preserve_all:
    OF.emitSimpleVariant("PreserveAll");
    break;
  case AttributedType::attr_regcall:
    OF.emitSimpleVariant("Regcall");
    break;
  }
}

//@atd type objc_lifetime_attr = [
//@atd   | OCL_None
//@atd   | OCL_ExplicitNone
//@atd   | OCL_Strong
//@atd   | OCL_Weak
//@atd   | OCL_Autoreleasing
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpObjCLifetimeQual(
    Qualifiers::ObjCLifetime qual) {
  switch (qual) {
  case Qualifiers::ObjCLifetime::OCL_None:
    OF.emitSimpleVariant("OCL_None");
    break;
  case Qualifiers::ObjCLifetime::OCL_ExplicitNone:
    OF.emitSimpleVariant("OCL_ExplicitNone");
    break;
  case Qualifiers::ObjCLifetime::OCL_Strong:
    OF.emitSimpleVariant("OCL_Strong");
    break;
  case Qualifiers::ObjCLifetime::OCL_Weak:
    OF.emitSimpleVariant("OCL_Weak");
    break;
  case Qualifiers::ObjCLifetime::OCL_Autoreleasing:
    OF.emitSimpleVariant("OCL_Autoreleasing");
    break;
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::AttributedTypeTupleSize() {
  return TypeTupleSize() + 1;
}

//@atd #define attributed_type_tuple type_tuple * attr_type_info
//@atd type attr_type_info = {
//@atd   attr_kind : type_attribute_kind;
//@atd   ~lifetime <ocaml default="`OCL_None"> : objc_lifetime_attr
//@atd } <ocaml field_prefix="ati_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitAttributedType(const AttributedType *T) {
  VisitType(T);
  Qualifiers quals = QualType(T, 0).getQualifiers();

  bool hasLifetimeQual =
      quals.hasObjCLifetime() &&
      quals.getObjCLifetime() != Qualifiers::ObjCLifetime::OCL_None;
  ObjectScope Scope(OF, 1 + hasLifetimeQual);
  OF.emitTag("attr_kind");
  dumpTypeAttr(T->getAttrKind());
  if (hasLifetimeQual) {
    OF.emitTag("lifetime");
    dumpObjCLifetimeQual(quals.getObjCLifetime());
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::BlockPointerTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define block_pointer_type_tuple type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBlockPointerType(const BlockPointerType *T) {
  VisitType(T);
  dumpQualType(T->getPointeeType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::BuiltinTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define builtin_type_tuple type_tuple * builtin_type_kind
//@atd type builtin_type_kind = [
#define BUILTIN_TYPE(TYPE, ID) //@atd   | TYPE
#include <clang/AST/BuiltinTypes.def>
//@atd ]
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitBuiltinType(const BuiltinType *T) {
  VisitType(T);
  std::string type_name;
  switch (T->getKind()) {
#define BUILTIN_TYPE(TYPE, ID) \
  case BuiltinType::TYPE: {    \
    type_name = #TYPE;         \
    break;                     \
  }
#include <clang/AST/BuiltinTypes.def>
#define IMAGE_TYPE(ImgType, ID, SingletonId, Access, Suffix) \
  case BuiltinType::ID:
#include <clang/Basic/OpenCLImageTypes.def>
    llvm_unreachable("OCL builtin types are unsupported");
    break;
  }
  OF.emitSimpleVariant(type_name);
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::DecltypeTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define decltype_type_tuple type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDecltypeType(const DecltypeType *T) {
  VisitType(T);
  dumpQualType(T->getUnderlyingType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FunctionTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define function_type_tuple type_tuple * function_type_info
//@atd type function_type_info = {
//@atd   return_type : qual_type
//@atd } <ocaml field_prefix="fti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFunctionType(const FunctionType *T) {
  VisitType(T);
  ObjectScope Scope(OF, 1);
  OF.emitTag("return_type");
  dumpQualType(T->getReturnType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::FunctionProtoTypeTupleSize() {
  return FunctionTypeTupleSize() + 1;
}
//@atd #define function_proto_type_tuple function_type_tuple * params_type_info
//@atd type params_type_info = {
//@atd   ~params_type : qual_type list
//@atd } <ocaml field_prefix="pti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitFunctionProtoType(
    const FunctionProtoType *T) {
  VisitFunctionType(T);

  bool HasParamsType = T->getNumParams() > 0;
  ObjectScope Scope(OF, 0 + HasParamsType);

  if (HasParamsType) {
    OF.emitTag("params_type");
    ArrayScope aScope(OF, T->getParamTypes().size());
    for (const auto &paramType : T->getParamTypes()) {
      dumpQualType(paramType);
    }
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::MemberPointerTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define member_pointer_type_tuple type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitMemberPointerType(
    const MemberPointerType *T) {
  VisitType(T);
  dumpQualType(T->getPointeeType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCObjectPointerTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define obj_c_object_pointer_type_tuple qual_type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCObjectPointerType(
    const ObjCObjectPointerType *T) {
  VisitType(T);
  dumpQualType(T->getPointeeType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCObjectTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define obj_c_object_type_tuple type_tuple * objc_object_type_info
//@atd type objc_object_type_info = {
//@atd   base_type : type_ptr;
//@atd   ~protocol_decls_ptr : pointer list;
//@atd   ~type_args : qual_type list;
//@atd } <ocaml field_prefix="ooti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCObjectType(const ObjCObjectType *T) {
  VisitType(T);

  int numProtocols = T->getNumProtocols();
  bool HasProtocols = numProtocols > 0;
  bool isSpecialized = T->isSpecialized();
  ObjectScope Scope(OF, 1 + HasProtocols + isSpecialized);

  OF.emitTag("base_type");
  dumpQualTypeNoQuals(T->getBaseType());

  if (HasProtocols) {
    OF.emitTag("protocol_decls_ptr");
    ArrayScope aScope(OF, numProtocols);
    for (int i = 0; i < numProtocols; i++) {
      dumpPointer(T->getProtocol(i));
    }
  }

  if (isSpecialized) {
    OF.emitTag("type_args");
    ArrayScope aScope(OF, T->getTypeArgs().size());
    for (auto &argType : T->getTypeArgs()) {
      dumpQualType(argType);
    };
  }
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ObjCInterfaceTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define obj_c_interface_type_tuple type_tuple * pointer
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitObjCInterfaceType(
    const ObjCInterfaceType *T) {
  // skip VisitObjCObjectType deliberately - ObjCInterfaceType can't have any
  // protocols

  VisitType(T);
  dumpPointer(T->getDecl());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ParenTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define paren_type_tuple type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitParenType(const ParenType *T) {
  // this is just syntactic sugar
  VisitType(T);
  dumpQualType(T->getInnerType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::PointerTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define pointer_type_tuple qual_type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitPointerType(const PointerType *T) {
  VisitType(T);
  dumpQualType(T->getPointeeType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::ReferenceTypeTupleSize() {
  return TypeWithChildInfoTupleSize();
}
//@atd #define reference_type_tuple qual_type_with_child_info
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitReferenceType(const ReferenceType *T) {
  VisitType(T);
  dumpQualType(T->getPointeeType());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TagTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define tag_type_tuple type_tuple * pointer
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTagType(const TagType *T) {
  VisitType(T);
  dumpPointer(T->getDecl());
}

template <class ATDWriter>
int ASTExporter<ATDWriter>::TypedefTypeTupleSize() {
  return TypeTupleSize() + 1;
}
//@atd #define typedef_type_tuple type_tuple * typedef_type_info
//@atd type typedef_type_info = {
//@atd   child_type : qual_type;
//@atd   decl_ptr : pointer;
//@atd } <ocaml field_prefix="tti_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitTypedefType(const TypedefType *T) {
  VisitType(T);
  ObjectScope Scope(OF, 2);
  OF.emitTag("child_type");
  dumpQualType(T->desugar());
  OF.emitTag("decl_ptr");
  dumpPointer(T->getDecl());
}

//@atd type c_type = [
#define TYPE(CLASS, PARENT) //@atd   | CLASS@@Type of (@CLASS@_type_tuple)
#define ABSTRACT_TYPE(CLASS, PARENT)
TYPE(None, Type)
#include <clang/AST/TypeNodes.def>
//@atd ] <ocaml repr="classic" validator="Clang_ast_visit.visit_type">

template <class ATDWriter = JsonWriter, bool ForceYojson = false>
class ExporterASTConsumer : public ASTConsumer {
 private:
  std::shared_ptr<ASTExporterOptions> options;
  std::unique_ptr<raw_ostream> OS;

 public:
  using ASTConsumerOptions = ASTLib::ASTExporterOptions;
  using PreprocessorHandler = ASTPluginLib::EmptyPreprocessorHandler;
  using PreprocessorHandlerData = ASTPluginLib::EmptyPreprocessorHandlerData;

  ExporterASTConsumer(const CompilerInstance &CI,
                      std::shared_ptr<ASTConsumerOptions> options,
                      std::shared_ptr<PreprocessorHandlerData> sharedData,
                      std::unique_ptr<raw_ostream> &&OS)
      : options(options), OS(std::move(OS)) {
    if (ForceYojson) {
      options->atdWriterOptions.useYojson = true;
    }
  }

  virtual void HandleTranslationUnit(ASTContext &Context) {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    ASTExporter<ATDWriter> P(*OS, Context, *options);
    P.dumpDecl(D);
  }
};

typedef ASTPluginLib::SimplePluginASTAction<
    ASTLib::ExporterASTConsumer<ASTLib::JsonWriter, false>>
    JsonExporterASTAction;
typedef ASTPluginLib::SimplePluginASTAction<
    ASTLib::ExporterASTConsumer<ASTLib::JsonWriter, true>>
    YojsonExporterASTAction;
typedef ASTPluginLib::SimplePluginASTAction<
    ASTLib::ExporterASTConsumer<ATDWriter::BiniouWriter<llvm::raw_ostream>,
                                true>>
    BiniouExporterASTAction;

} // end of namespace ASTLib
