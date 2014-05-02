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

(* misc I/O *)

(* maps '-' to the standard input *)
let open_in name =
  if name = "-" then stdin else Pervasives.open_in name

(* maps '-' to the standard output *)
let open_out name =
  if name = "-" then stdout else Pervasives.open_out name

(* missing string API *)

let string_ends_with s1 s2 =
  try
    let n = String.length s2 in
    (String.sub s1 (String.length s1 - n) n) = s2
  with Invalid_argument _ -> false

(* missing stream API *)

let line_stream_of_channel channel =
    Stream.from
      (fun _ ->
         try Some (input_line channel) with End_of_file -> None)

let stream_concat streams =
  let current_stream = ref None in
  let rec next i =
      try
        let stream =
          match !current_stream with
          | Some stream -> stream
          | None ->
            let stream = Stream.next streams in
             current_stream := Some stream;
             stream in
        try Some (Stream.next stream)
        with Stream.Failure -> (current_stream := None; next i)
      with Stream.Failure -> None in
    Stream.from next

let stream_append s1 s2 = stream_concat (Stream.of_list [s1; s2])

let stream_map f stream =
  let rec next i =
      try Some (f (Stream.next stream))
      with Stream.Failure -> None in
    Stream.from next

let stream_filter p stream =
  let rec next i =
      try
        let value = Stream.next stream in
        if p value then Some value else next i
      with Stream.Failure -> None in
    Stream.from next

let stream_fold f init stream =
  let result = ref init in
    Stream.iter
      (fun x -> result := f x !result)
      stream;
    !result

let stream_to_list s =
  List.rev (stream_fold (fun x l -> x::l) [] s)

(* simplistic unit testing *)

let assert_true s b =
  if not b then begin
    prerr_endline s;
    exit 1
  end else ()

let assert_false s b = assert_true s (not b)
let assert_equal s x y = assert_true s (x = y)
