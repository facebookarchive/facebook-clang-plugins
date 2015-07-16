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
let index_decl_type_pointers top_decl =
	(* just in case *)
	Clang_ast_cache.declMap := Clang_ast_cache.PointerMap.empty; 
	Clang_ast_cache.typeMap := Clang_ast_cache.PointerMap.empty;
	(* populate caches *)
	ignore(Clang_ast_v.validate_decl [] top_decl);

	let result = !Clang_ast_cache.declMap, !Clang_ast_cache.typeMap in
	Clang_ast_cache.declMap := Clang_ast_cache.PointerMap.empty;
	Clang_ast_cache.typeMap := Clang_ast_cache.PointerMap.empty;
	result
