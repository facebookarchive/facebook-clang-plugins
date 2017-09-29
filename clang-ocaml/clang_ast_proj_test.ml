(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

open Utils
open Clang_ast_t
open Clang_ast_proj

let source_location ?file ?line ?column () =
{
  sl_file = file;
  sl_line = line;
  sl_column = column;
}

let empty_source_location = source_location ()

let decl_info start stop = {
  di_pointer = 0;
  di_parent_pointer = None;
  di_source_range = (start, stop) ;
  di_owning_module = None;
  di_is_hidden = false;
  di_is_implicit = false;
  di_is_used = false;
  di_is_this_declaration_referenced = false;
  di_is_invalid_decl = false;
  di_attributes = [];
  di_full_comment = None;
  di_access = `None
}

let name_info name = {
  ni_name = name;
  ni_qual_name = [name]
}

let append_name_info info suffix = {
  ni_name = info.ni_name ^ suffix;
  ni_qual_name = List.map (fun x -> x ^ suffix) info.ni_qual_name
}

let () =
  let di = decl_info empty_source_location empty_source_location
  in
  let decl = LabelDecl(di, name_info "foo")
  in
  assert_equal "get_decl_kind_string" (get_decl_kind_string decl) "LabelDecl";
  assert_equal "get_decl_tuple" (get_decl_tuple decl) di;
  assert_equal "get_decl_context_tuple" (get_decl_context_tuple decl) None;
  assert_equal "get_named_decl_tuple" (get_named_decl_tuple decl) (Some (di, name_info "foo"));
  assert_equal "get_type_decl_tuple" (get_type_decl_tuple decl) None;
  assert_equal "get_tag_decl_tuple" (get_tag_decl_tuple decl) None;
  assert_equal "is_valid_astnode_kind" (is_valid_astnode_kind (get_decl_kind_string decl)) true;
  assert_equal "is_valid_astnode_kind" (is_valid_astnode_kind "AFakeNodeThatDoesNotExist") false;

  let decl2 = update_named_decl_tuple (fun (di, info) -> (di, append_name_info info "bar")) decl
  in
  assert_equal "update_named_decl_tuple" (get_named_decl_tuple decl2) (Some (di, name_info "foobar"));

  let di2 = decl_info (source_location ~file:"bla" ()) (source_location ~file:"bleh" ())
  in
  let decl3 = update_decl_tuple (fun _ -> di2) decl
  in
  assert_equal "update_decl_tuple" (get_decl_tuple decl3) di2
