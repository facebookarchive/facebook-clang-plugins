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

let validate_decl_from_file fname =
  let ast = Ag_util.Json.from_file Clang_ast_j.read_decl fname
  in
  Ag_util.Json.to_channel Clang_ast_j.write_decl stdout ast;
  print_newline ()

let main =
  let v = Sys.argv
  in
  try
    for i = 1 to Array.length v - 1 do
      validate_decl_from_file v.(i)
    done
  with
    Yojson.Json_error s
  | Ag_oj_run.Error s -> begin
    prerr_string s;
    prerr_newline ();
    exit 1
  end
