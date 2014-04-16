open Process

let main =
  let pid, ic = fork (fun oc -> output_string oc "This is a test\n"; true)
  in
  let zipunzip = compose gzip gunzip
  in
  diff_on_same_input (fun ic oc -> copy ic oc; true) zipunzip ic stdout
