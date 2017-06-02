(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

open Clang_ast_t

val get_decl_kind_string : decl -> string
val get_decl_tuple : decl -> (decl_tuple)
val get_decl_context_tuple : decl -> (decl_context_tuple) option
val get_named_decl_tuple : decl -> (named_decl_tuple) option
val get_type_decl_tuple : decl -> (type_decl_tuple) option
val get_tag_decl_tuple : decl -> (tag_decl_tuple) option

val get_stmt_kind_string : stmt -> string
val get_stmt_tuple : stmt -> (stmt_tuple)
val get_expr_tuple : stmt -> (expr_tuple) option

val get_type_tuple : c_type -> (type_tuple)

val update_decl_tuple : ((decl_tuple) -> (decl_tuple)) -> decl -> decl
val update_decl_context_tuple : ((decl_context_tuple) -> (decl_context_tuple)) -> decl -> decl
val update_named_decl_tuple : ((named_decl_tuple) -> (named_decl_tuple)) -> decl -> decl
val update_type_decl_tuple : ((type_decl_tuple) -> (type_decl_tuple)) -> decl -> decl
val update_tag_decl_tuple : ((tag_decl_tuple) -> (tag_decl_tuple)) -> decl -> decl

val update_stmt_tuple : ((stmt_tuple) -> (stmt_tuple)) -> stmt -> stmt
val update_expr_tuple : ((expr_tuple) -> (expr_tuple)) -> stmt -> stmt

val is_valid_binop_kind_name : string -> bool
val is_valid_unop_kind_name : string -> bool

val string_of_binop_kind : binary_operator_kind -> string
val string_of_unop_kind : unary_operator_kind -> string
val is_valid_astnode_kind : string -> bool
val string_of_cast_kind : cast_kind -> string
val get_cast_kind : stmt -> cast_kind option
