(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

val read_data_from_file : 'a Ag_util.Json.reader -> string -> 'a
val write_data_to_file :
  ?pretty:bool -> ?compact_json:bool -> ?std_json:bool -> 'a Ag_util.Json.writer -> string -> 'a -> unit

val ydump : ?compact_json:bool -> ?std_json:bool -> in_channel -> out_channel -> bool

val empty_string : string

val run_converter_tool : 'a Ag_util.Json.reader -> 'a Ag_util.Json.writer -> unit
