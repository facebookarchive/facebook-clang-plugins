(*
 *  Copyright (c) 2015 Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

exception PointerMismatch

let validate_ptr key_ptr node =
  let node_ptr = Clang_ast_main.get_ptr_from_node node in
  if node_ptr != key_ptr then raise PointerMismatch

let validate_decl_ptr key_ptr decl =
  validate_ptr key_ptr (`DeclNode decl)

let validate_stmt_ptr key_ptr stmt =
  validate_ptr key_ptr (`StmtNode stmt)

let validate_type_ptr key_ptr c_type =
  validate_ptr key_ptr (`TypeNode c_type)

let print_node path kind_str =
  let indent = String.make (List.length path) ' ' in
  prerr_string (indent);
  prerr_string (kind_str);
  prerr_newline ()

let print_decl path decl =
  print_node path (Clang_ast_proj.get_decl_kind_string decl)

let print_stmt path stmt =
  print_node path (Clang_ast_proj.get_stmt_kind_string stmt)


let print_map_size map =
  let sum_el k v acc = acc + 1 in
  let s = Clang_ast_main.PointerMap.fold sum_el map 0 in
  prerr_string (string_of_int s ^ " ")

let check_decl_cache_from_file fname =
  let ast = Ag_util.Json.from_file Clang_ast_j.read_decl fname in
  let decl_cache, stmt_cache, type_cache = Clang_ast_main.index_node_pointers ast in
  print_map_size decl_cache;
  print_map_size stmt_cache;
  print_map_size type_cache;
  prerr_newline ();
  Clang_ast_main.PointerMap.iter validate_decl_ptr decl_cache;
  Clang_ast_main.PointerMap.iter validate_stmt_ptr stmt_cache;
  Clang_ast_main.PointerMap.iter validate_type_ptr type_cache;
  Clang_ast_main.visit_ast ~visit_decl:print_decl ~visit_stmt:print_stmt ast

let main =
  let v = Sys.argv
  in
  try
    for i = 1 to Array.length v - 1 do
      check_decl_cache_from_file v.(i);
    done
  with
  | PointerMismatch ->
    prerr_string "Pointer in cache doesn't match";
    exit 1
  | Yojson.Json_error s
  | Ag_oj_run.Error s -> begin
    prerr_string s;
    prerr_newline ();
    exit 1
  end
