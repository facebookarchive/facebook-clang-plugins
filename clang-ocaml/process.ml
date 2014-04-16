(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

module U = Unix

let file_exists name =
  try
    U.access name [U.F_OK];
    true
  with
    U.Unix_error _ -> false

let mktemp base =
  let rec aux count =
    let name = Printf.sprintf "%s.%03d" base count in
    if file_exists name then aux (count + 1) else name
  in
  if file_exists base then aux 0 else base

let tee ic ocs =
  let buffer_size = 8192 in
  let buffer = String.create buffer_size in
  let rec tee_loop () = match input ic buffer 0 buffer_size with
    | 0 -> ()
    | r -> List.iter (fun oc -> output oc buffer 0 r) ocs; tee_loop ()
  in
  tee_loop ()

let copy ic oc = tee ic [oc]

let wait pid = match snd (U.waitpid [U.WNOHANG] pid) with
  | U.WEXITED 0 -> true
  | _ -> false

let exec args stdin stdout stderr =
  wait (U.create_process args.(0) args stdin stdout stderr)

let diff file1 file2 oc =
  exec [| "diff"; file1; file2 |] U.stdin (U.descr_of_out_channel oc) U.stderr

let gzip ic oc =
  exec [| "gzip"; "-c" |] (U.descr_of_in_channel ic) (U.descr_of_out_channel oc) U.stderr

let gunzip ic oc =
  exec [| "gunzip"; "-c" |] (U.descr_of_in_channel ic) (U.descr_of_out_channel oc) U.stderr

let fork f =
  let (fd_in, fd_out) = U.pipe () in
  match U.fork () with
  | 0 -> begin
    U.close fd_in;
    if f (U.out_channel_of_descr fd_out)
    then exit 0
    else exit 1
  end
  | pid -> begin
    U.close fd_out;
    (pid, U.in_channel_of_descr fd_in)
  end

let compose f g ic oc =
  let (pid, ic1) = fork (f ic)
  in
  if g ic1 oc then wait pid else false

let diff_on_same_input f1 f2 ic oc =
  let file = mktemp "input"
  in
  let ofile = open_out file
  in
  copy ic ofile;
  close_out ofile;
  let file1 = mktemp "output1"
  and file2 = mktemp "output2"
  in
  let ofile1 = open_out file1
  and ofile2 = open_out file2
  in
  let success =
    if f1 (open_in file) ofile1 then
      if f2 (open_in file) ofile2
      then begin
        close_out ofile1;
        close_out ofile2;
        diff file1 file2 oc
      end else false
    else false
  in
  U.unlink file;
  U.unlink file1;
  U.unlink file2;
  success
