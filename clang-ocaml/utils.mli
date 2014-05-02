(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  Copyright (c) 2014, ocaml.org
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

val open_in : string -> in_channel
val open_out : string -> out_channel

val string_ends_with : string -> string -> bool

val line_stream_of_channel : in_channel -> string Stream.t
val stream_concat : 'a Stream.t Stream.t -> 'a Stream.t
val stream_append : 'a Stream.t -> 'a Stream.t -> 'a Stream.t
val stream_map : ('a -> 'b) -> 'a Stream.t -> 'b Stream.t
val stream_filter : ('a -> bool) -> 'a Stream.t -> 'a Stream.t
val stream_fold : ('a -> 'b -> 'b) -> 'b -> 'a Stream.t -> 'b
val stream_to_list : 'a Stream.t -> 'a list

val assert_true : string -> bool -> unit
val assert_false : string -> bool -> unit
val assert_equal : string -> 'a -> 'a -> unit
