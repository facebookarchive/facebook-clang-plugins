(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

open Utils
open Process

let () =
  assert_true "string_ends_with::1" (string_ends_with "foo" "o");
  assert_false "string_ends_with::2" (string_ends_with "foo" "adw")

let () =
  let pid, ic = Process.fork (fun oc -> output_string oc "foo\nbar\n"; true)
  in
  let lines = stream_to_list (line_stream_of_channel ic)
  in
  assert_true "line_stream_of_channel::wait" (Process.wait pid);
  assert_equal "line_stream_of_channel::result" lines ["foo"; "bar"]

let () =
  let lines = stream_to_list (stream_append (Stream.of_list ["foo"]) (Stream.of_list ["bar"]))
  in
  assert_equal "line_stream_of_channel::result" lines ["foo"; "bar"]
