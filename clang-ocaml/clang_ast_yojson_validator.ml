(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

let main = Yojson_utils.make_yojson_validation_tool Clang_ast_j.read_decl Clang_ast_j.write_decl Sys.argv
