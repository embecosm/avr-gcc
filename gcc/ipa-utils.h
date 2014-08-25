/* Utilities for ipa analysis.
   Copyright (C) 2004-2014 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_IPA_UTILS_H
#define GCC_IPA_UTILS_H
#include "cgraph.h"

struct ipa_dfs_info {
  int dfn_number;
  int low_link;
  /* This field will have the samy value for any two nodes in the same strongly
     connected component.  */
  int scc_no;
  bool new_node;
  bool on_stack;
  struct cgraph_node* next_cycle;
  PTR aux;
};

/* Context of polymorphic call.  This is used by ipa-devirt walkers of the
   type inheritance graph.  */

class ipa_polymorphic_call_context {
public:
  /* The called object appears in an object of type OUTER_TYPE
     at offset OFFSET.  When information is not 100% reliable, we
     use SPECULATIVE_OUTER_TYPE and SPECULATIVE_OFFSET. */
  HOST_WIDE_INT offset;
  HOST_WIDE_INT speculative_offset;
  tree outer_type;
  tree speculative_outer_type;
  /* True if outer object may be in construction or destruction.  */
  bool maybe_in_construction;
  /* True if outer object may be of derived type.  */
  bool maybe_derived_type;
  /* True if speculative outer object may be of derived type.  We always
     speculate that construction does not happen.  */
  bool speculative_maybe_derived_type;

  /* Build empty "I know nothing" context.  */
  ipa_polymorphic_call_context ();

  /* Build polymorphic call context for indirect call E.  */
  ipa_polymorphic_call_context (cgraph_edge *e);

  /* Make context non-speculative.  */
  void clear_speculation ();

  /* Walk container types and modify context to point to actual class
     containing EXPECTED_TYPE as base class.  */
  bool restrict_to_inner_class (tree expected_type);

  /* Look for vtable stores or constructor calls to work out dynamic type
     of memory location.  */
  bool get_dynamic_type (tree, tree, tree, gimple);
};

/* Build polymorphic call context for indirect call E.  */

inline
ipa_polymorphic_call_context::ipa_polymorphic_call_context (cgraph_edge *e)
{
  offset = e->indirect_info->offset;
  speculative_offset = e->indirect_info->speculative_offset;
  outer_type = e->indirect_info->outer_type;
  speculative_outer_type = e->indirect_info->speculative_outer_type;
  maybe_in_construction = e->indirect_info->maybe_in_construction;
  maybe_derived_type = e->indirect_info->maybe_derived_type;
  speculative_maybe_derived_type = e->indirect_info->speculative_maybe_derived_type;
}

/* Build empty "I know nothing" context.  */

inline
ipa_polymorphic_call_context::ipa_polymorphic_call_context ()
 : offset(0), speculative_offset(0), outer_type(NULL),
   speculative_outer_type(NULL), maybe_in_construction(false),
   maybe_derived_type(false), speculative_maybe_derived_type(false)
{
}

/* Make context non-speculative.  */

inline void
ipa_polymorphic_call_context::clear_speculation ()
{
  speculative_outer_type = NULL;
  speculative_offset = 0;
  speculative_maybe_derived_type = false;
}

/* In ipa-utils.c  */
void ipa_print_order (FILE*, const char *, struct cgraph_node**, int);
int ipa_reduced_postorder (struct cgraph_node **, bool, bool,
			  bool (*ignore_edge) (struct cgraph_edge *));
void ipa_free_postorder_info (void);
vec<cgraph_node *> ipa_get_nodes_in_cycle (struct cgraph_node *);
bool ipa_edge_within_scc (struct cgraph_edge *);
int ipa_reverse_postorder (struct cgraph_node **);
tree get_base_var (tree);
void ipa_merge_profiles (struct cgraph_node *dst,
			 struct cgraph_node *src);
bool recursive_call_p (tree, tree);

/* In ipa-profile.c  */
bool ipa_propagate_frequency (struct cgraph_node *node);

/* In ipa-devirt.c  */

struct odr_type_d;
typedef odr_type_d *odr_type;
void build_type_inheritance_graph (void);
void update_type_inheritance_graph (void);
vec <cgraph_node *>
possible_polymorphic_call_targets (tree, HOST_WIDE_INT,
				   ipa_polymorphic_call_context,
				   bool *final = NULL,
				   void **cache_token = NULL,
				   int *nonconstruction_targets = NULL);
odr_type get_odr_type (tree, bool insert = false);
void dump_possible_polymorphic_call_targets (FILE *, tree, HOST_WIDE_INT,
					     const ipa_polymorphic_call_context &);
bool possible_polymorphic_call_target_p (tree, HOST_WIDE_INT,
				         const ipa_polymorphic_call_context &,
					 struct cgraph_node *);
tree method_class_type (const_tree);
tree get_polymorphic_call_info (tree, tree, tree *,
				HOST_WIDE_INT *,
				ipa_polymorphic_call_context *,
				gimple call = NULL);
bool get_polymorphic_call_info_from_invariant (ipa_polymorphic_call_context *,
					       tree, tree, HOST_WIDE_INT);
bool decl_maybe_in_construction_p (tree, tree, gimple, tree);
tree vtable_pointer_value_to_binfo (const_tree);
bool vtable_pointer_value_to_vtable (const_tree, tree *, unsigned HOST_WIDE_INT *);
void compare_virtual_tables (varpool_node *, varpool_node *);
bool contains_polymorphic_type_p (const_tree);

/* Return vector containing possible targets of polymorphic call E.
   If FINALP is non-NULL, store true if the list is complette. 
   CACHE_TOKEN (if non-NULL) will get stored to an unique ID of entry
   in the target cache.  If user needs to visit every target list
   just once, it can memoize them.

   Returned vector is placed into cache.  It is NOT caller's responsibility
   to free it.  The vector can be freed on cgraph_remove_node call if
   the particular node is a virtual function present in the cache.  */

inline vec <cgraph_node *>
possible_polymorphic_call_targets (struct cgraph_edge *e,
				   bool *final = NULL,
				   void **cache_token = NULL,
				   int *nonconstruction_targets = NULL)
{
  gcc_checking_assert (e->indirect_info->polymorphic);
  ipa_polymorphic_call_context context(e);
  return possible_polymorphic_call_targets (e->indirect_info->otr_type,
					    e->indirect_info->otr_token,
					    context,
					    final, cache_token,
					    nonconstruction_targets);
}

/* Same as above but taking OBJ_TYPE_REF as an parameter.  */

inline vec <cgraph_node *>
possible_polymorphic_call_targets (tree ref,
				   gimple call,
				   bool *final = NULL,
				   void **cache_token = NULL)
{
  tree otr_type;
  HOST_WIDE_INT otr_token;
  ipa_polymorphic_call_context context;

  get_polymorphic_call_info (current_function_decl,
			     ref,
			     &otr_type, &otr_token, &context, call);
  return possible_polymorphic_call_targets (obj_type_ref_class (ref),
					    tree_to_uhwi
					      (OBJ_TYPE_REF_TOKEN (ref)),
					    context,
					    final, cache_token);
}

/* Dump possible targets of a polymorphic call E into F.  */

inline void
dump_possible_polymorphic_call_targets (FILE *f, struct cgraph_edge *e)
{
  gcc_checking_assert (e->indirect_info->polymorphic);
  ipa_polymorphic_call_context context(e);
  dump_possible_polymorphic_call_targets (f, e->indirect_info->otr_type,
					  e->indirect_info->otr_token,
					  context);
}

/* Return true if N can be possibly target of a polymorphic call of
   E.  */

inline bool
possible_polymorphic_call_target_p (struct cgraph_edge *e,
				    struct cgraph_node *n)
{
  ipa_polymorphic_call_context context(e);
  return possible_polymorphic_call_target_p (e->indirect_info->otr_type,
					     e->indirect_info->otr_token,
					     context, n);
}

/* Return true if N can be possibly target of a polymorphic call of
   OBJ_TYPE_REF expression CALL.  */

inline bool
possible_polymorphic_call_target_p (tree call,
				    struct cgraph_node *n)
{
  ipa_polymorphic_call_context context;
  return possible_polymorphic_call_target_p (obj_type_ref_class (call),
					     tree_to_uhwi
					       (OBJ_TYPE_REF_TOKEN (call)),
					     context,
					     n);
}
#endif  /* GCC_IPA_UTILS_H  */


