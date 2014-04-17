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
val write_data_to_file : ?pretty:bool -> 'a Ag_util.Json.writer -> string -> 'a -> unit

val ydump : in_channel -> out_channel -> bool

val make_yojson_validator :
  'a Ag_util.Json.reader -> 'a Ag_util.Json.writer -> string array -> unit
