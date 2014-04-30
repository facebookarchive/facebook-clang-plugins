(*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

let ends_with s1 s2 =
  try
    let n = String.length s2 in
    (String.sub s1 (String.length s1 - n) n) = s2
  with Invalid_argument _ -> false

let open_in name =
  if name = "-" then stdin else Pervasives.open_in name

let open_out name =
  if name = "-" then stdout else Pervasives.open_out name

let ydump ?(compact_json=false) ?(std_json=false) ic oc =
  let cmd = "ydump" ^ (if compact_json then " -c" else "") ^ (if std_json then " -std" else "")
  in
  Process.exec [| cmd |] ic oc stderr

let read_data_from_file reader fname =
  let input_gunzipped read_data ic =
    let pid, icz = Process.fork (Process.gunzip ic) in
    let data = read_data icz in
    let r = Process.wait pid in
    Process.close_in icz;
    if not r then failwith "read_data_from_file (gunzip)" else ();
    data
  in
  let ic = open_in fname in
  let data =
    if ends_with fname ".value.gz" then
      input_gunzipped Marshal.from_channel ic
    else if ends_with fname ".gz" then
      input_gunzipped (Ag_util.Json.from_channel ~fname reader) ic
    else if ends_with fname ".value" then
      Marshal.from_channel ic
    else
      Ag_util.Json.from_channel ~fname reader ic
  in
  close_in ic;
  data

let write_data_to_file ?(pretty=false) ?(compact_json=false) ?(std_json=false) writer fname data =
  let output_gzipped write_data oc data =
    let pid, icz = Process.fork (fun ocz -> write_data ocz data; true) in
    let r1 = Process.gzip icz oc
    and r2 = Process.wait pid
    in
    Process.close_in icz;
    if not (r1 && r2) then failwith "write_data_from_file (gzip)" else ()
  and output_pretty write_data oc data =
    (* TODO(mathieubaudet): find out how to write directly pretty json? *)
    let pid, icp = Process.fork (fun ocp -> write_data ocp data; true) in
    let r1 = ydump ~compact_json ~std_json icp oc
    and r2 = Process.wait pid
    in
    Process.close_in icp;
    if not (r1 && r2) then failwith "write_data_from_file (pretty)" else ();
  in
  let write_json ocx data =
    if pretty then
      output_pretty (Ag_util.Json.to_channel writer) ocx data
    else
      Ag_util.Json.to_channel writer ocx data
  in
  let oc = open_out fname
  in
  if ends_with fname ".value.gz" then
    output_gzipped (fun oc data -> Marshal.to_channel oc data []) oc data
  else if ends_with fname ".value" then
    Marshal.to_channel oc data []
  else if ends_with fname ".gz" then
    output_gzipped write_json oc data
  else
    write_json oc data;
  close_out oc

let validate ?(compact_json=false) ?(std_json=false) reader writer fname =
  let read_write ic oc =
    try
      let data = Ag_util.Json.from_channel ~fname reader ic
      in
      Ag_util.Json.to_channel writer oc data;
      true
    with
    | Yojson.Json_error s
    | Ag_oj_run.Error s ->
      begin
        prerr_string s;
        prerr_newline ();
        false
      end
  in
  let ydump ic oc = ydump ~compact_json ~std_json ic oc
  in
  let read_write_ydump = Process.compose read_write ydump
  in
  if ends_with fname ".gz" then
    Process.diff_on_same_input
      (Process.compose Process.gunzip ydump)
      (Process.compose Process.gunzip read_write_ydump)
      (open_in fname)
      stdout
  else
    Process.diff_on_same_input ydump read_write_ydump (open_in fname) stdout

let make_yojson_validator ?(compact_json=false) ?(std_json=false) reader writer argv =
  let process_file name = ignore (validate ~compact_json ~std_json reader writer name)
  in 
  if Array.length argv > 1 then
    for i = 1 to Array.length argv - 1 do
      process_file argv.(i)
    done
  else
    try
      while true do
        process_file (read_line ())
      done;
    with End_of_file -> ()

let convert ?(pretty=false) ?(compact_json=false) ?(std_json=false) reader writer fin fout =
  try
    read_data_from_file reader fin
    |> write_data_to_file writer ~pretty ~compact_json ~std_json fout
  with
  | Yojson.Json_error s
  | Ag_oj_run.Error s ->
    begin
      prerr_string s;
      prerr_newline ();
      exit 1
    end

let make_converter reader writer argv =
  let len = Array.length argv
  in
  let i, pretty =
    if len > 1 && argv.(1) = "--pretty"
    then 2, true
    else 1, false
  in
  let process = convert ~pretty reader writer
  in 
  if i + 1 < len then
    process argv.(i) argv.(i+1)
  else if i < len then
    process argv.(i) (argv.(i) ^ ".value.gz")
  else begin
    Printf.fprintf stderr "Usage: %s [--pretty] INPUT_FILE [OUTPUT_FILE]\n" argv.(0);
    Printf.fprintf stderr "Parse yojson values and convert them to another format based on the extension of the output file (default: ${INPUT_FILE}.value.gz).\n";
    exit 1
  end
