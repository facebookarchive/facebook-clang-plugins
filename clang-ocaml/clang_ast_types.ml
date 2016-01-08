(*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

(* Default implementation to add more structure to some fields *)
(* Replace this file to get different implementation *)

(* Type pointers *)
type t_ptr = [ | `TPtr of int ]

let pointer_to_type_ptr raw = `TPtr raw

let type_ptr_to_pointer type_ptr = match type_ptr with `TPtr raw -> raw
