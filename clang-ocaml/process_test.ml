(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

open Process

let main =
  let pid, ic = fork (fun oc -> output_string oc "This is a test\n"; true)
  in
  let zipunzip = compose gzip gunzip
  in
  ignore (diff_on_same_input (fun ic oc -> copy ic oc; flush_all (); true) zipunzip ic stderr);
  close_in ic;
  ignore (wait pid)
