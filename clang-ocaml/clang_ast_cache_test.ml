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

let validate_ptr key_ptr decl =
  let decl_info = Clang_ast_proj.get_decl_tuple decl in
  let ptr = decl_info.Clang_ast_t.di_pointer in
  if ptr != key_ptr then raise PointerMismatch

let check_decl_cache_from_file fname =
  let ast = Ag_util.Json.from_file Clang_ast_j.read_decl fname in
  let cache = Clang_ast_main.index_decl_pointers ast in
  Clang_ast_cache.PointerMap.iter validate_ptr cache

let main =
  let v = Sys.argv
  in
  try
    for i = 1 to Array.length v - 1 do
      check_decl_cache_from_file v.(i)
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
