(*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

module PointerOrd = struct
    type t = Clang_ast_t.pointer
    let compare = String.compare
  end
module PointerMap = Map.Make(PointerOrd)

let declMap = ref PointerMap.empty

let add_decl_to_cache decl =
	let decl_info = Clang_ast_proj.get_decl_tuple decl in
	let ptr = decl_info.Clang_ast_t.di_pointer in
	declMap := PointerMap.add ptr decl !declMap;
	(* return true to pass atd validation *)
	true
