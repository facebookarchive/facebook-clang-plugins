(*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

(* Type pointers *)
module TypePtr = struct

(* extensible type to allow users to specify more variants *)
type t = ..
type t += Ptr of int

let wrap x = Ptr x
let unwrap = function | Ptr x -> x | _ -> raise (invalid_arg "Unknown variant type")

end
