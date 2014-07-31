Guidelines for writing ATD annotations in ASTExporter.cpp
=========================================================

The ATD specifications inlined in ASTExporter.cpp are used to generate Ocaml parsers using 'atdgen'. Those annotations must reflect
closely the Yojson (or Json) emitted by the C++ plugins.

The ATD language and the parser generating tool 'atdgen' are documented here:
  http://mjambon.com/atdgen/atdgen-manual.html

ATD basics
----------

The definition of Yojson object in ASTExporter.cpp typically look like this:
```
/// \atd
/// type record = {
///  mandatory_field : string
///  ?optional_int : int option
///  ~string_empty_by_default : string
/// } <ocaml field_prefix="r_">
```

It must be a `///`-style comments starting with `\atd`.

The `?` symbols mean that an absent field is ok and maps to the ocaml value `None`.
The `~` symbols mean that an absent field is ok and maps to some default value for this type.

The `<ocaml field_prefix="r_">` annotations are currently required to disambiguate records on the ocaml side. The prefix should be
made the first letters of the C++ types, except for a few exceptions (e.g. `CXX` is mapped to `x`).

Valid Yojson values for this specification are for instance:
```
{ "mandatory_field" : "foo" }
{ "mandatory_field" : "foo", "optional_int" : 3 }
```

Simple example
--------------

```
/// type source_location = {
///   ?file : string option;
///   ?line : int option;
///   ?column : int option;
/// } <ocaml field_prefix="sl_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::dumpBareSourceLocation(SourceLocation Loc) { // "Bare" indicates that we emit a Yojson object not preceded by a tag.
  ObjectScope Scope(OF);                                                  // Outputs a Yojson object. The closing brace will be added when 'Scope' is destroyed.
                                                                          // This is typical in C++ : http://en.wikipedia.org/wiki/Resource_Acquisition_Is_Initialization
  SourceLocation SpellingLoc = SM.getSpellingLoc(Loc);
  PresumedLoc PLoc = SM.getPresumedLoc(SpellingLoc);
  if (PLoc.isInvalid()) {
    return;                                                               // Early return is ok because all Yojson fields are optional.
  }

  if (strcmp(PLoc.getFilename(), LastLocFilename) != 0) {
    OF.emitTag("file");                                                   // emits the tag of the field 'file'
    OF.emitString(FileUtils::normalizePath(BasePath, PLoc.getFilename()));// emits a string (since we do emit something, the ocaml value is 'Some (...)')
    OF.emitTag("line");
    OF.emitInteger(PLoc.getLine());
    OF.emitTag("column");
    OF.emitInteger(PLoc.getColumn());
  }

  // ...
}
```

Note that parser expects the C++ code to emit the corresponding fields in the same order.

More complex example
--------------------

```
/// \atd
/// type decl_ref = {
///   kind : decl_kind;                (* ATD type declared below *)
///   ?name : string;
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
  OF.emitSimpleVariant(D.getDeclKindName());            // case of an algebraic datatype that carries no value (like a C enum field)
  const NamedDecl *ND = dyn_cast<NamedDecl>(&D);
  if (ND) {
    OF.emitTag("name");
    OF.emitString(ND->getNameAsString());
    OF.emitFlag("is_hidden", ND->isHidden());           // flags correspond to ATD fields of the form "~tag : bool"
  }
  if (const ValueDecl *VD = dyn_cast<ValueDecl>(&D)) {  // ok not to output anything because the field is optional
    dumpQualType(VD->getType());                        // will emit the tagged Yojson value for a QualType
  }
}
```

The complex definition for decl_kind is processed in several stages.

First we use an adequate node of the clang preprocessor to expand the #include and create the string:
```
/// type decl_kind = [
/// | AccessSpec
/// | Block
/// | Captured
/// (* ... *)
/// ]
```

Then we extract the ATD specifications using different python scripts.
```
type decl_kind = [
| AccessSpec
| Block
| Captured
(* ... *)
]
```

After calling atdgen, the final Ocaml type is:
```
type decl_kind = `AccessSpec | `Block | `Captured | `ClassScopeFunctionSpecialization  (* ... *)
```

Testing
-------

Compiling with `DEBUG=1` will make the ATDWriter enforce the general well-formedness of the emitted Yojson. For instance, a missing tag will trigger an assert failure.

Discrepancies between annotations and emitted Yojson are detected by the unit tests in ../clang-ocaml.


Mapping clang AST nodes to ocaml values
---------------------------------------

Clang AST entities of a given type are typically represented by a cluster of classes.

For instance, here is the cluster for declarations: http://clang.llvm.org/doxygen/classclang_1_1Decl.html

To map these entities to a flat algebraic data type of Ocaml (serialized as a Yojson "variant" by ATDWriter), as seen
before, we heavily rely on a (hacky) C-preprocessing stage and several scripts.

Let us study how declarations are handled more precisely.

##### Prelude (default values for node tuples)
```
/// \atd
#define DECL(DERIVED, BASE) /// #define @DERIVED@_decl_tuple @BASE@_tuple
#define ABSTRACT_DECL(DECL) DECL
#include <clang/AST/DeclNodes.inc>
```

After one step of preprocessing + ATD-extraction, this creates the following intermediate code (see `build/ast_inline.atd.inc`)
```
#define access_spec_decl_tuple decl_tuple
#define block_decl_tuple decl_tuple
#define captured_decl_tuple decl_tuple
// ...
#define named_decl_tuple decl_tuple
#define decl_context_tuple decl list * decl_context_info
```

This defines the default value of each `xxxx_decl_tuple` to be that of the base class.

The `@...@` signs are processed by python macros in `libtooling/atdlib`. For instance, `@CaptureDecl@` gives `capture_decl`.

##### Corpus (overriding node tuples, actually outputting data)

When the visiting method for nodes of given type is effectively written, it is expected that the
corresponding `#define xxxx_decl_tuple` is overwritten to add the specific information of the kind of nodes.

```
/// \atd
/// #define decl_tuple decl_info
/// type decl_info = {
///    (* ... *)
/// } <ocaml field_prefix="di_">
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitDecl(const Decl *D) { // This is the top class. Everything here concerns all declarations nodes.
// ...
}

/// \atd
/// #define named_decl_tuple decl_tuple * string   (* must start with the tuple of the base class *)
template <class ATDWriter>
void ASTExporter<ATDWriter>::VisitNamedDecl(const NamedDecl *D) { // Some important intermediate abstract class NamedDecl.
  VisitDecl(D);                                                   // Must visit the base class to output its tuple of information.
  OF.emitString(D->getNameAsString());                            // Extra information for the derived class.
}
```

##### Conclusion (variant type)

The final definitions of the `xxx_decl_tuple` are meant to be inlined in the declaration of the actual sum type for all declarations.

```
// main variant for declarations
/// \atd
/// type decl = [
#define DECL(DERIVED, BASE)   ///   | DERIVED@@Decl of (@DERIVED@_decl_tuple)
#define ABSTRACT_DECL(DECL)
#include <clang/AST/DeclNodes.inc>
/// ]

```

This expands first to: (see `build/ast_inline.atd.p`)
```
type decl = [
| AccessSpecDecl of (access_spec_decl_tuple)
| BlockDecl of (block_decl_tuple)
| CapturedDecl of (captured_decl_tuple)
| ClassScopeFunctionSpecializationDecl of (class_scope_function_specialization_decl_tuple)
(* ... *)
]
```

Then after a last stage of preprocessing: (see `build/clang_ast.atd`)
```
type decl = [
    AccessSpecDecl of (decl_info)
  | BlockDecl
      of (decl_info * decl list * decl_context_info * block_decl_info)
  | CapturedDecl of (decl_info * decl list * decl_context_info)
  | ClassScopeFunctionSpecializationDecl of (decl_info)
(* ... *)
]
```
