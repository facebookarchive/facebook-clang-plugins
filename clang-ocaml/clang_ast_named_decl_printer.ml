(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

module P = Printf

let rec visit_named_decls f decl =
  let () =
    match Clang_ast_proj.get_named_decl_tuple decl with
      Some (x, y) -> f x y
    | None -> ()
  in
  match Clang_ast_proj.get_decl_context_tuple decl with
    Some (l, _) -> List.iter (visit_named_decls f) l
  | None -> ()


let print_named_decl_from_file fname =
  let ast = Ag_util.Json.from_file Clang_ast_j.read_decl fname in
  let getname name_info = name_info.Clang_ast_t.ni_name in
  visit_named_decls (fun _ info -> print_string (getname info); print_newline ()) ast

let main =
  let v = Sys.argv
  in
  try
    for i = 1 to Array.length v - 1 do
      print_named_decl_from_file v.(i)
    done
  with
    Yojson.Json_error s
  | Ag_oj_run.Error s -> begin
    prerr_string s;
    prerr_newline ();
    exit 1
  end
