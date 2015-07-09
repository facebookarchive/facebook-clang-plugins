(*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

(* This function is not thread-safe *)
let index_decl_pointers top_decl =
	Clang_ast_cache.declMap := Clang_ast_cache.PointerMap.empty; (* just in case *)
	ignore(Clang_ast_v.validate_decl [] top_decl); (* populate cache *)
	let result = !Clang_ast_cache.declMap in
	Clang_ast_cache.declMap := Clang_ast_cache.PointerMap.empty;
	result
