(*
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *)

open Utils
open Clang_ast_t
open Clang_ast_proj

let source_location ?file ?line ?column () = {sl_file= file; sl_line= line; sl_column= column}

let empty_source_location = source_location ()

let decl_info start stop =
  { di_pointer= 0
  ; di_parent_pointer= None
  ; di_source_range= (start, stop)
  ; di_owning_module= None
  ; di_is_hidden= false
  ; di_is_implicit= false
  ; di_is_used= false
  ; di_is_this_declaration_referenced= false
  ; di_is_invalid_decl= false
  ; di_attributes= []
  ; di_full_comment= None
  ; di_access= `None }


let name_info name = {ni_name= name; ni_qual_name= [name]}

let append_name_info info suffix =
  {ni_name= info.ni_name ^ suffix; ni_qual_name= List.map (fun x -> x ^ suffix) info.ni_qual_name}

let qual_type ptr =
  { qt_type_ptr= ptr
  ; qt_is_const= false
  ; qt_is_restrict= false
  ; qt_is_volatile= false }

let var_decl_info ~is_global =
  { vdi_is_global= is_global
  ; vdi_is_extern= false
  ; vdi_is_static_local= false
  ; vdi_is_static_data_member= false
  ; vdi_is_const_expr= false
  ; vdi_is_init_ice= false
  ; vdi_is_init_expr_cxx11_constant= false
  ; vdi_init_expr= None
  ; vdi_parm_index_in_function= None
  ; vdi_storage_class= None }


let () =
  let di = decl_info empty_source_location empty_source_location in
  let decl = LabelDecl (di, name_info "foo") in
  assert_equal "get_decl_kind_string" (get_decl_kind_string decl) "LabelDecl" ;
  assert_equal "get_decl_tuple" (get_decl_tuple decl) di ;
  assert_equal "get_decl_context_tuple" (get_decl_context_tuple decl) None ;
  assert_equal "get_named_decl_tuple" (get_named_decl_tuple decl) (Some (di, name_info "foo")) ;
  assert_equal "get_type_decl_tuple" (get_type_decl_tuple decl) None ;
  assert_equal "get_tag_decl_tuple" (get_tag_decl_tuple decl) None ;
  assert_equal "is_valid_astnode_kind" (is_valid_astnode_kind (get_decl_kind_string decl)) true ;
  assert_equal "is_valid_astnode_kind" (is_valid_astnode_kind "AFakeNodeThatDoesNotExist") false ;
  let decl2 = update_named_decl_tuple (fun (di, info) -> (di, append_name_info info "bar")) decl in
  assert_equal "update_named_decl_tuple" (get_named_decl_tuple decl2)
    (Some (di, name_info "foobar")) ;
  let di2 = decl_info (source_location ~file:"bla" ()) (source_location ~file:"bleh" ()) in
  let decl3 = update_decl_tuple (fun _ -> di2) decl in
  assert_equal "update_decl_tuple" (get_decl_tuple decl3) di2 ;

  assert_equal "get_var_decl_tuple_none" (get_var_decl_tuple decl) None ;
  let vdi = var_decl_info ~is_global:true in
  let qt = qual_type (Clang_ast_types.TypePtr.wrap 0) in
  let var_decl = ParmVarDecl(di, name_info "fooey", qt, vdi) in
  assert_equal "get_var_decl_tuple" (get_var_decl_tuple var_decl)
    (Some (di, name_info "fooey", qt, vdi)) ;
  let updated_var_decl = update_var_decl_tuple
      (fun (di, ni, qt, vdi) ->
         (di, append_name_info ni "-mod", qt, var_decl_info ~is_global:false))
      var_decl in
  assert_equal "update_var_decl_tuple" (get_var_decl_tuple updated_var_decl)
    (Some (di, name_info "fooey-mod", qt, var_decl_info ~is_global:false)) ;
