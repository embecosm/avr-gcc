/* Basic IPA optimizations and utilities.
   Copyright (C) 2003-2014 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "calls.h"
#include "stringpool.h"
#include "cgraph.h"
#include "tree-pass.h"
#include "pointer-set.h"
#include "gimple-expr.h"
#include "gimplify.h"
#include "flags.h"
#include "target.h"
#include "tree-iterator.h"
#include "ipa-utils.h"
#include "ipa-inline.h"
#include "tree-inline.h"
#include "profile.h"
#include "params.h"
#include "internal-fn.h"
#include "tree-ssa-alias.h"
#include "gimple.h"
#include "dbgcnt.h"


/* Return true when NODE has ADDR reference.  */

static bool
has_addr_references_p (struct cgraph_node *node,
		       void *data ATTRIBUTE_UNUSED)
{
  int i;
  struct ipa_ref *ref;

  for (i = 0; ipa_ref_list_referring_iterate (&node->ref_list,
					      i, ref); i++)
    if (ref->use == IPA_REF_ADDR)
      return true;
  return false;
}

/* Look for all functions inlined to NODE and update their inlined_to pointers
   to INLINED_TO.  */

static void
update_inlined_to_pointer (struct cgraph_node *node, struct cgraph_node *inlined_to)
{
  struct cgraph_edge *e;
  for (e = node->callees; e; e = e->next_callee)
    if (e->callee->global.inlined_to)
      {
        e->callee->global.inlined_to = inlined_to;
	update_inlined_to_pointer (e->callee, inlined_to);
      }
}

/* Add symtab NODE to queue starting at FIRST.

   The queue is linked via AUX pointers and terminated by pointer to 1.
   We enqueue nodes at two occasions: when we find them reachable or when we find
   their bodies needed for further clonning.  In the second case we mark them
   by pointer to 2 after processing so they are re-queue when they become
   reachable.  */

static void
enqueue_node (symtab_node *node, symtab_node **first,
	      struct pointer_set_t *reachable)
{
  /* Node is still in queue; do nothing.  */
  if (node->aux && node->aux != (void *) 2)
    return;
  /* Node was already processed as unreachable, re-enqueue
     only if it became reachable now.  */
  if (node->aux == (void *)2 && !pointer_set_contains (reachable, node))
    return;
  node->aux = *first;
  *first = node;
}

/* Process references.  */

static void
process_references (struct ipa_ref_list *list,
		    symtab_node **first,
		    bool before_inlining_p,
		    struct pointer_set_t *reachable)
{
  int i;
  struct ipa_ref *ref;
  for (i = 0; ipa_ref_list_reference_iterate (list, i, ref); i++)
    {
      symtab_node *node = ref->referred;

      if (node->definition && !node->in_other_partition
	  && ((!DECL_EXTERNAL (node->decl) || node->alias)
	      || (((before_inlining_p
		    && (cgraph_state < CGRAPH_STATE_IPA_SSA
		        || !lookup_attribute ("always_inline",
					      DECL_ATTRIBUTES (node->decl)))))
		  /* We use variable constructors during late complation for
		     constant folding.  Keep references alive so partitioning
		     knows about potential references.  */
		  || (TREE_CODE (node->decl) == VAR_DECL
		      && flag_wpa
		      && ctor_for_folding (node->decl)
		         != error_mark_node))))
	pointer_set_insert (reachable, node);
      enqueue_node (node, first, reachable);
    }
}

/* EDGE is an polymorphic call.  If BEFORE_INLINING_P is set, mark
   all its potential targets as reachable to permit later inlining if
   devirtualization happens.  After inlining still keep their declarations
   around, so we can devirtualize to a direct call.

   Also try to make trivial devirutalization when no or only one target is
   possible.  */

static void
walk_polymorphic_call_targets (pointer_set_t *reachable_call_targets,
			       struct cgraph_edge *edge,
			       symtab_node **first,
			       pointer_set_t *reachable, bool before_inlining_p)
{
  unsigned int i;
  void *cache_token;
  bool final;
  vec <cgraph_node *>targets
    = possible_polymorphic_call_targets
	(edge, &final, &cache_token);

  if (!pointer_set_insert (reachable_call_targets,
			   cache_token))
    {
      for (i = 0; i < targets.length (); i++)
	{
	  struct cgraph_node *n = targets[i];

	  /* Do not bother to mark virtual methods in anonymous namespace;
	     either we will find use of virtual table defining it, or it is
	     unused.  */
	  if (TREE_CODE (TREE_TYPE (n->decl)) == METHOD_TYPE
	      && type_in_anonymous_namespace_p
		    (method_class_type (TREE_TYPE (n->decl))))
	    continue;

	  /* Prior inlining, keep alive bodies of possible targets for
	     devirtualization.  */
	   if (n->definition
	       && (before_inlining_p
		   && (cgraph_state < CGRAPH_STATE_IPA_SSA
		       || !lookup_attribute ("always_inline",
					     DECL_ATTRIBUTES (n->decl)))))
	     pointer_set_insert (reachable, n);

	  /* Even after inlining we want to keep the possible targets in the
	     boundary, so late passes can still produce direct call even if
	     the chance for inlining is lost.  */
	  enqueue_node (n, first, reachable);
	}
    }

  /* Very trivial devirtualization; when the type is
     final or anonymous (so we know all its derivation)
     and there is only one possible virtual call target,
     make the edge direct.  */
  if (final)
    {
      if (targets.length () <= 1 && dbg_cnt (devirt))
	{
	  cgraph_node *target, *node = edge->caller;
	  if (targets.length () == 1)
	    target = targets[0];
	  else
	    target = cgraph_get_create_node
		       (builtin_decl_implicit (BUILT_IN_UNREACHABLE));

	  if (dump_enabled_p ())
            {
              location_t locus = gimple_location (edge->call_stmt);
              dump_printf_loc (MSG_OPTIMIZED_LOCATIONS, locus,
                               "devirtualizing call in %s/%i to %s/%i\n",
                               edge->caller->name (), edge->caller->order,
                               target->name (),
                               target->order);
	    }
	  edge = cgraph_make_edge_direct (edge, target);
	  if (inline_summary_vec)
	    inline_update_overall_summary (node);
	  else if (edge->call_stmt)
	    cgraph_redirect_edge_call_stmt_to_callee (edge);
	}
    }
}

/* Perform reachability analysis and reclaim all unreachable nodes.

   The algorithm is basically mark&sweep but with some extra refinements:

   - reachable extern inline functions needs special handling; the bodies needs
     to stay in memory until inlining in hope that they will be inlined.
     After inlining we release their bodies and turn them into unanalyzed
     nodes even when they are reachable.

     BEFORE_INLINING_P specify whether we are before or after inlining.

   - virtual functions are kept in callgraph even if they seem unreachable in
     hope calls to them will be devirtualized. 

     Again we remove them after inlining.  In late optimization some
     devirtualization may happen, but it is not important since we won't inline
     the call. In theory early opts and IPA should work out all important cases.

   - virtual clones needs bodies of their origins for later materialization;
     this means that we want to keep the body even if the origin is unreachable
     otherwise.  To avoid origin from sitting in the callgraph and being
     walked by IPA passes, we turn them into unanalyzed nodes with body
     defined.

     We maintain set of function declaration where body needs to stay in
     body_needed_for_clonning

     Inline clones represent special case: their declaration match the
     declaration of origin and cgraph_remove_node already knows how to
     reshape callgraph and preserve body when offline copy of function or
     inline clone is being removed.

   - C++ virtual tables keyed to other unit are represented as DECL_EXTERNAL
     variables with DECL_INITIAL set.  We finalize these and keep reachable
     ones around for constant folding purposes.  After inlining we however
     stop walking their references to let everything static referneced by them
     to be removed when it is otherwise unreachable.

   We maintain queue of both reachable symbols (i.e. defined symbols that needs
   to stay) and symbols that are in boundary (i.e. external symbols referenced
   by reachable symbols or origins of clones).  The queue is represented
   as linked list by AUX pointer terminated by 1.

   At the end we keep all reachable symbols. For symbols in boundary we always
   turn definition into a declaration, but we may keep function body around
   based on body_needed_for_clonning

   All symbols that enter the queue have AUX pointer non-zero and are in the
   boundary.  Pointer set REACHABLE is used to track reachable symbols.

   Every symbol can be visited twice - once as part of boundary and once
   as real reachable symbol. enqueue_node needs to decide whether the
   node needs to be re-queued for second processing.  For this purpose
   we set AUX pointer of processed symbols in the boundary to constant 2.  */

bool
symtab_remove_unreachable_nodes (bool before_inlining_p, FILE *file)
{
  symtab_node *first = (symtab_node *) (void *) 1;
  struct cgraph_node *node, *next;
  varpool_node *vnode, *vnext;
  bool changed = false;
  struct pointer_set_t *reachable = pointer_set_create ();
  struct pointer_set_t *body_needed_for_clonning = pointer_set_create ();
  struct pointer_set_t *reachable_call_targets = pointer_set_create ();

  timevar_push (TV_IPA_UNREACHABLE);
#ifdef ENABLE_CHECKING
  verify_symtab ();
#endif
  if (optimize && flag_devirtualize)
    build_type_inheritance_graph ();
  if (file)
    fprintf (file, "\nReclaiming functions:");
#ifdef ENABLE_CHECKING
  FOR_EACH_FUNCTION (node)
    gcc_assert (!node->aux);
  FOR_EACH_VARIABLE (vnode)
    gcc_assert (!vnode->aux);
#endif
  /* Mark functions whose bodies are obviously needed.
     This is mostly when they can be referenced externally.  Inline clones
     are special since their declarations are shared with master clone and thus
     cgraph_can_remove_if_no_direct_calls_and_refs_p should not be called on them.  */
  FOR_EACH_FUNCTION (node)
    {
      node->used_as_abstract_origin = false;
      if (node->definition
	  && !node->global.inlined_to
	  && !node->in_other_partition
	  && !cgraph_can_remove_if_no_direct_calls_and_refs_p (node))
	{
	  gcc_assert (!node->global.inlined_to);
	  pointer_set_insert (reachable, node);
	  enqueue_node (node, &first, reachable);
	}
      else
	gcc_assert (!node->aux);
     }

  /* Mark variables that are obviously needed.  */
  FOR_EACH_DEFINED_VARIABLE (vnode)
    if (!varpool_can_remove_if_no_refs (vnode)
	&& !vnode->in_other_partition)
      {
	pointer_set_insert (reachable, vnode);
	enqueue_node (vnode, &first, reachable);
      }

  /* Perform reachability analysis.  */
  while (first != (symtab_node *) (void *) 1)
    {
      bool in_boundary_p = !pointer_set_contains (reachable, first);
      symtab_node *node = first;

      first = (symtab_node *)first->aux;

      /* If we are processing symbol in boundary, mark its AUX pointer for
	 possible later re-processing in enqueue_node.  */
      if (in_boundary_p)
	node->aux = (void *)2;
      else
	{
	  if (TREE_CODE (node->decl) == FUNCTION_DECL
	      && DECL_ABSTRACT_ORIGIN (node->decl))
	    {
	      struct cgraph_node *origin_node
	      = cgraph_get_create_node (DECL_ABSTRACT_ORIGIN (node->decl));
	      origin_node->used_as_abstract_origin = true;
	      enqueue_node (origin_node, &first, reachable);
	    }
	  /* If any symbol in a comdat group is reachable, force
	     all externally visible symbols in the same comdat
	     group to be reachable as well.  Comdat-local symbols
	     can be discarded if all uses were inlined.  */
	  if (node->same_comdat_group)
	    {
	      symtab_node *next;
	      for (next = node->same_comdat_group;
		   next != node;
		   next = next->same_comdat_group)
		if (!symtab_comdat_local_p (next)
		    && !pointer_set_insert (reachable, next))
		  enqueue_node (next, &first, reachable);
	    }
	  /* Mark references as reachable.  */
	  process_references (&node->ref_list, &first,
			      before_inlining_p, reachable);
	}

      if (cgraph_node *cnode = dyn_cast <cgraph_node *> (node))
	{
	  /* Mark the callees reachable unless they are direct calls to extern
 	     inline functions we decided to not inline.  */
	  if (!in_boundary_p)
	    {
	      struct cgraph_edge *e;
	      /* Keep alive possible targets for devirtualization.  */
	      if (optimize && flag_devirtualize)
		{
		  struct cgraph_edge *next;
		  for (e = cnode->indirect_calls; e; e = next)
		    {
		      next = e->next_callee;
		      if (e->indirect_info->polymorphic)
			walk_polymorphic_call_targets (reachable_call_targets,
						       e, &first, reachable,
						       before_inlining_p);
		    }
		}
	      for (e = cnode->callees; e; e = e->next_callee)
		{
		  if (e->callee->definition
		      && !e->callee->in_other_partition
		      && (!e->inline_failed
			  || !DECL_EXTERNAL (e->callee->decl)
			  || e->callee->alias
			  || before_inlining_p))
		    {
		      /* Be sure that we will not optimize out alias target
			 body.  */
		      if (DECL_EXTERNAL (e->callee->decl)
			  && e->callee->alias
			  && before_inlining_p)
			{
		          pointer_set_insert (reachable,
					      cgraph_function_node (e->callee));
			}
		      pointer_set_insert (reachable, e->callee);
		    }
		  enqueue_node (e->callee, &first, reachable);
		}

	      /* When inline clone exists, mark body to be preserved so when removing
		 offline copy of the function we don't kill it.  */
	      if (cnode->global.inlined_to)
	        pointer_set_insert (body_needed_for_clonning, cnode->decl);

	      /* For non-inline clones, force their origins to the boundary and ensure
		 that body is not removed.  */
	      while (cnode->clone_of)
		{
		  bool noninline = cnode->clone_of->decl != cnode->decl;
		  cnode = cnode->clone_of;
		  if (noninline)
		    {
		      pointer_set_insert (body_needed_for_clonning, cnode->decl);
		      enqueue_node (cnode, &first, reachable);
		    }
		}

	    }
	  /* If any reachable function has simd clones, mark them as
	     reachable as well.  */
	  if (cnode->simd_clones)
	    {
	      cgraph_node *next;
	      for (next = cnode->simd_clones;
		   next;
		   next = next->simdclone->next_clone)
		if (in_boundary_p
		    || !pointer_set_insert (reachable, next))
		  enqueue_node (next, &first, reachable);
	    }
	}
      /* When we see constructor of external variable, keep referred nodes in the
	boundary.  This will also hold initializers of the external vars NODE
	refers to.  */
      varpool_node *vnode = dyn_cast <varpool_node *> (node);
      if (vnode
	  && DECL_EXTERNAL (node->decl)
	  && !vnode->alias
	  && in_boundary_p)
	{
	  struct ipa_ref *ref;
	  for (int i = 0; ipa_ref_list_reference_iterate (&node->ref_list, i, ref); i++)
	    enqueue_node (ref->referred, &first, reachable);
	}
    }

  /* Remove unreachable functions.   */
  for (node = cgraph_first_function (); node; node = next)
    {
      next = cgraph_next_function (node);

      /* If node is not needed at all, remove it.  */
      if (!node->aux)
	{
	  if (file)
	    fprintf (file, " %s/%i", node->name (), node->order);
	  cgraph_remove_node (node);
	  changed = true;
	}
      /* If node is unreachable, remove its body.  */
      else if (!pointer_set_contains (reachable, node))
        {
	  if (!pointer_set_contains (body_needed_for_clonning, node->decl))
	    cgraph_release_function_body (node);
	  else if (!node->clone_of)
	    gcc_assert (in_lto_p || DECL_RESULT (node->decl));
	  if (node->definition)
	    {
	      if (file)
		fprintf (file, " %s/%i", node->name (), node->order);
	      node->body_removed = true;
	      node->analyzed = false;
	      node->definition = false;
	      node->cpp_implicit_alias = false;
	      node->alias = false;
	      node->thunk.thunk_p = false;
	      node->weakref = false;
	      /* After early inlining we drop always_inline attributes on
		 bodies of functions that are still referenced (have their
		 address taken).  */
	      DECL_ATTRIBUTES (node->decl)
		= remove_attribute ("always_inline",
				    DECL_ATTRIBUTES (node->decl));
	      if (!node->in_other_partition)
		node->local.local = false;
	      cgraph_node_remove_callees (node);
	      symtab_remove_from_same_comdat_group (node);
	      ipa_remove_all_references (&node->ref_list);
	      changed = true;
	    }
	}
      else
	gcc_assert (node->clone_of || !cgraph_function_with_gimple_body_p (node)
		    || in_lto_p || DECL_RESULT (node->decl));
    }

  /* Inline clones might be kept around so their materializing allows further
     cloning.  If the function the clone is inlined into is removed, we need
     to turn it into normal cone.  */
  FOR_EACH_FUNCTION (node)
    {
      if (node->global.inlined_to
	  && !node->callers)
	{
	  gcc_assert (node->clones);
	  node->global.inlined_to = NULL;
	  update_inlined_to_pointer (node, node);
	}
      node->aux = NULL;
    }

  /* Remove unreachable variables.  */
  if (file)
    fprintf (file, "\nReclaiming variables:");
  for (vnode = varpool_first_variable (); vnode; vnode = vnext)
    {
      vnext = varpool_next_variable (vnode);
      if (!vnode->aux
	  /* For can_refer_decl_in_current_unit_p we want to track for
	     all external variables if they are defined in other partition
	     or not.  */
	  && (!flag_ltrans || !DECL_EXTERNAL (vnode->decl)))
	{
	  if (file)
	    fprintf (file, " %s/%i", vnode->name (), vnode->order);
	  varpool_remove_node (vnode);
	  changed = true;
	}
      else if (!pointer_set_contains (reachable, vnode))
        {
	  tree init;
	  if (vnode->definition)
	    {
	      if (file)
		fprintf (file, " %s", vnode->name ());
	      changed = true;
	    }
	  vnode->body_removed = true;
	  vnode->definition = false;
	  vnode->analyzed = false;
	  vnode->aux = NULL;

	  symtab_remove_from_same_comdat_group (vnode);

	  /* Keep body if it may be useful for constant folding.  */
	  if ((init = ctor_for_folding (vnode->decl)) == error_mark_node)
	    varpool_remove_initializer (vnode);
	  else
	    DECL_INITIAL (vnode->decl) = init;
	  ipa_remove_all_references (&vnode->ref_list);
	}
      else
	vnode->aux = NULL;
    }

  pointer_set_destroy (reachable);
  pointer_set_destroy (body_needed_for_clonning);
  pointer_set_destroy (reachable_call_targets);

  /* Now update address_taken flags and try to promote functions to be local.  */
  if (file)
    fprintf (file, "\nClearing address taken flags:");
  FOR_EACH_DEFINED_FUNCTION (node)
    if (node->address_taken
	&& !node->used_from_other_partition)
      {
	if (!cgraph_for_node_and_aliases (node, has_addr_references_p, NULL, true))
	  {
	    if (file)
	      fprintf (file, " %s", node->name ());
	    node->address_taken = false;
	    changed = true;
	    if (cgraph_local_node_p (node))
	      {
		node->local.local = true;
		if (file)
		  fprintf (file, " (local)");
	      }
	  }
      }
  if (file)
    fprintf (file, "\n");

#ifdef ENABLE_CHECKING
  verify_symtab ();
#endif

  /* If we removed something, perhaps profile could be improved.  */
  if (changed && optimize && inline_edge_summary_vec.exists ())
    FOR_EACH_DEFINED_FUNCTION (node)
      ipa_propagate_frequency (node);

  timevar_pop (TV_IPA_UNREACHABLE);
  return changed;
}

/* Process references to VNODE and set flags WRITTEN, ADDRESS_TAKEN, READ
   as needed, also clear EXPLICIT_REFS if the references to given variable
   do not need to be explicit.  */

void
process_references (varpool_node *vnode,
		    bool *written, bool *address_taken,
		    bool *read, bool *explicit_refs)
{
  int i;
  struct ipa_ref *ref;

  if (!varpool_all_refs_explicit_p (vnode)
      || TREE_THIS_VOLATILE (vnode->decl))
    *explicit_refs = false;

  for (i = 0; ipa_ref_list_referring_iterate (&vnode->ref_list,
					     i, ref)
	      && *explicit_refs && (!*written || !*address_taken || !*read); i++)
    switch (ref->use)
      {
      case IPA_REF_ADDR:
	*address_taken = true;
	break;
      case IPA_REF_LOAD:
	*read = true;
	break;
      case IPA_REF_STORE:
	*written = true;
	break;
      case IPA_REF_ALIAS:
	process_references (varpool (ref->referring), written, address_taken,
			    read, explicit_refs);
	break;
      }
}

/* Set TREE_READONLY bit.  */

bool
set_readonly_bit (varpool_node *vnode, void *data ATTRIBUTE_UNUSED)
{
  TREE_READONLY (vnode->decl) = true;
  return false;
}

/* Set writeonly bit and clear the initalizer, since it will not be needed.  */

bool
set_writeonly_bit (varpool_node *vnode, void *data ATTRIBUTE_UNUSED)
{
  vnode->writeonly = true;
  if (optimize)
    {
      DECL_INITIAL (vnode->decl) = NULL;
      if (!vnode->alias)
	ipa_remove_all_references (&vnode->ref_list);
    }
  return false;
}

/* Clear addressale bit of VNODE.  */

bool
clear_addressable_bit (varpool_node *vnode, void *data ATTRIBUTE_UNUSED)
{
  vnode->address_taken = false;
  TREE_ADDRESSABLE (vnode->decl) = 0;
  return false;
}

/* Discover variables that have no longer address taken or that are read only
   and update their flags.

   FIXME: This can not be done in between gimplify and omp_expand since
   readonly flag plays role on what is shared and what is not.  Currently we do
   this transformation as part of whole program visibility and re-do at
   ipa-reference pass (to take into account clonning), but it would
   make sense to do it before early optimizations.  */

void
ipa_discover_readonly_nonaddressable_vars (void)
{
  varpool_node *vnode;
  if (dump_file)
    fprintf (dump_file, "Clearing variable flags:");
  FOR_EACH_VARIABLE (vnode)
    if (!vnode->alias
	&& (TREE_ADDRESSABLE (vnode->decl)
	    || !vnode->writeonly
	    || !TREE_READONLY (vnode->decl)))
      {
	bool written = false;
	bool address_taken = false;
	bool read = false;
	bool explicit_refs = true;

	process_references (vnode, &written, &address_taken, &read, &explicit_refs);
	if (!explicit_refs)
	  continue;
	if (!address_taken)
	  {
	    if (TREE_ADDRESSABLE (vnode->decl) && dump_file)
	      fprintf (dump_file, " %s (non-addressable)", vnode->name ());
	    varpool_for_node_and_aliases (vnode, clear_addressable_bit, NULL, true);
	  }
	if (!address_taken && !written
	    /* Making variable in explicit section readonly can cause section
	       type conflict. 
	       See e.g. gcc.c-torture/compile/pr23237.c */
	    && DECL_SECTION_NAME (vnode->decl) == NULL)
	  {
	    if (!TREE_READONLY (vnode->decl) && dump_file)
	      fprintf (dump_file, " %s (read-only)", vnode->name ());
	    varpool_for_node_and_aliases (vnode, set_readonly_bit, NULL, true);
	  }
	if (!vnode->writeonly && !read && !address_taken && written)
	  {
	    if (dump_file)
	      fprintf (dump_file, " %s (write-only)", vnode->name ());
	    varpool_for_node_and_aliases (vnode, set_writeonly_bit, NULL, true);
	  }
      }
  if (dump_file)
    fprintf (dump_file, "\n");
}

/* Free inline summary.  */

namespace {

const pass_data pass_data_ipa_free_inline_summary =
{
  SIMPLE_IPA_PASS, /* type */
  "*free_inline_summary", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  true, /* has_execute */
  TV_IPA_FREE_INLINE_SUMMARY, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_ipa_free_inline_summary : public simple_ipa_opt_pass
{
public:
  pass_ipa_free_inline_summary (gcc::context *ctxt)
    : simple_ipa_opt_pass (pass_data_ipa_free_inline_summary, ctxt)
  {}

  /* opt_pass methods: */
  virtual unsigned int execute (function *)
    {
      inline_free_summary ();
      return 0;
    }

}; // class pass_ipa_free_inline_summary

} // anon namespace

simple_ipa_opt_pass *
make_pass_ipa_free_inline_summary (gcc::context *ctxt)
{
  return new pass_ipa_free_inline_summary (ctxt);
}

/* Generate and emit a static constructor or destructor.  WHICH must
   be one of 'I' (for a constructor) or 'D' (for a destructor).  BODY
   is a STATEMENT_LIST containing GENERIC statements.  PRIORITY is the
   initialization priority for this constructor or destructor. 

   FINAL specify whether the externally visible name for collect2 should
   be produced. */

static void
cgraph_build_static_cdtor_1 (char which, tree body, int priority, bool final)
{
  static int counter = 0;
  char which_buf[16];
  tree decl, name, resdecl;

  /* The priority is encoded in the constructor or destructor name.
     collect2 will sort the names and arrange that they are called at
     program startup.  */
  if (final)
    sprintf (which_buf, "%c_%.5d_%d", which, priority, counter++);
  else
  /* Proudce sane name but one not recognizable by collect2, just for the
     case we fail to inline the function.  */
    sprintf (which_buf, "sub_%c_%.5d_%d", which, priority, counter++);
  name = get_file_function_name (which_buf);

  decl = build_decl (input_location, FUNCTION_DECL, name,
		     build_function_type_list (void_type_node, NULL_TREE));
  current_function_decl = decl;

  resdecl = build_decl (input_location,
			RESULT_DECL, NULL_TREE, void_type_node);
  DECL_ARTIFICIAL (resdecl) = 1;
  DECL_RESULT (decl) = resdecl;
  DECL_CONTEXT (resdecl) = decl;

  allocate_struct_function (decl, false);

  TREE_STATIC (decl) = 1;
  TREE_USED (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (decl) = 1;
  DECL_SAVED_TREE (decl) = body;
  if (!targetm.have_ctors_dtors && final)
    {
      TREE_PUBLIC (decl) = 1;
      DECL_PRESERVE_P (decl) = 1;
    }
  DECL_UNINLINABLE (decl) = 1;

  DECL_INITIAL (decl) = make_node (BLOCK);
  TREE_USED (DECL_INITIAL (decl)) = 1;

  DECL_SOURCE_LOCATION (decl) = input_location;
  cfun->function_end_locus = input_location;

  switch (which)
    {
    case 'I':
      DECL_STATIC_CONSTRUCTOR (decl) = 1;
      decl_init_priority_insert (decl, priority);
      break;
    case 'D':
      DECL_STATIC_DESTRUCTOR (decl) = 1;
      decl_fini_priority_insert (decl, priority);
      break;
    default:
      gcc_unreachable ();
    }

  gimplify_function_tree (decl);

  cgraph_add_new_function (decl, false);

  set_cfun (NULL);
  current_function_decl = NULL;
}

/* Generate and emit a static constructor or destructor.  WHICH must
   be one of 'I' (for a constructor) or 'D' (for a destructor).  BODY
   is a STATEMENT_LIST containing GENERIC statements.  PRIORITY is the
   initialization priority for this constructor or destructor.  */

void
cgraph_build_static_cdtor (char which, tree body, int priority)
{
  cgraph_build_static_cdtor_1 (which, body, priority, false);
}

/* A vector of FUNCTION_DECLs declared as static constructors.  */
static vec<tree> static_ctors;
/* A vector of FUNCTION_DECLs declared as static destructors.  */
static vec<tree> static_dtors;

/* When target does not have ctors and dtors, we call all constructor
   and destructor by special initialization/destruction function
   recognized by collect2.

   When we are going to build this function, collect all constructors and
   destructors and turn them into normal functions.  */

static void
record_cdtor_fn (struct cgraph_node *node)
{
  if (DECL_STATIC_CONSTRUCTOR (node->decl))
    static_ctors.safe_push (node->decl);
  if (DECL_STATIC_DESTRUCTOR (node->decl))
    static_dtors.safe_push (node->decl);
  node = cgraph_get_node (node->decl);
  DECL_DISREGARD_INLINE_LIMITS (node->decl) = 1;
}

/* Define global constructors/destructor functions for the CDTORS, of
   which they are LEN.  The CDTORS are sorted by initialization
   priority.  If CTOR_P is true, these are constructors; otherwise,
   they are destructors.  */

static void
build_cdtor (bool ctor_p, vec<tree> cdtors)
{
  size_t i,j;
  size_t len = cdtors.length ();

  i = 0;
  while (i < len)
    {
      tree body;
      tree fn;
      priority_type priority;

      priority = 0;
      body = NULL_TREE;
      j = i;
      do
	{
	  priority_type p;
	  fn = cdtors[j];
	  p = ctor_p ? DECL_INIT_PRIORITY (fn) : DECL_FINI_PRIORITY (fn);
	  if (j == i)
	    priority = p;
	  else if (p != priority)
	    break;
	  j++;
	}
      while (j < len);

      /* When there is only one cdtor and target supports them, do nothing.  */
      if (j == i + 1
	  && targetm.have_ctors_dtors)
	{
	  i++;
	  continue;
	}
      /* Find the next batch of constructors/destructors with the same
	 initialization priority.  */
      for (;i < j; i++)
	{
	  tree call;
	  fn = cdtors[i];
	  call = build_call_expr (fn, 0);
	  if (ctor_p)
	    DECL_STATIC_CONSTRUCTOR (fn) = 0;
	  else
	    DECL_STATIC_DESTRUCTOR (fn) = 0;
	  /* We do not want to optimize away pure/const calls here.
	     When optimizing, these should be already removed, when not
	     optimizing, we want user to be able to breakpoint in them.  */
	  TREE_SIDE_EFFECTS (call) = 1;
	  append_to_statement_list (call, &body);
	}
      gcc_assert (body != NULL_TREE);
      /* Generate a function to call all the function of like
	 priority.  */
      cgraph_build_static_cdtor_1 (ctor_p ? 'I' : 'D', body, priority, true);
    }
}

/* Comparison function for qsort.  P1 and P2 are actually of type
   "tree *" and point to static constructors.  DECL_INIT_PRIORITY is
   used to determine the sort order.  */

static int
compare_ctor (const void *p1, const void *p2)
{
  tree f1;
  tree f2;
  int priority1;
  int priority2;

  f1 = *(const tree *)p1;
  f2 = *(const tree *)p2;
  priority1 = DECL_INIT_PRIORITY (f1);
  priority2 = DECL_INIT_PRIORITY (f2);

  if (priority1 < priority2)
    return -1;
  else if (priority1 > priority2)
    return 1;
  else
    /* Ensure a stable sort.  Constructors are executed in backwarding
       order to make LTO initialize braries first.  */
    return DECL_UID (f2) - DECL_UID (f1);
}

/* Comparison function for qsort.  P1 and P2 are actually of type
   "tree *" and point to static destructors.  DECL_FINI_PRIORITY is
   used to determine the sort order.  */

static int
compare_dtor (const void *p1, const void *p2)
{
  tree f1;
  tree f2;
  int priority1;
  int priority2;

  f1 = *(const tree *)p1;
  f2 = *(const tree *)p2;
  priority1 = DECL_FINI_PRIORITY (f1);
  priority2 = DECL_FINI_PRIORITY (f2);

  if (priority1 < priority2)
    return -1;
  else if (priority1 > priority2)
    return 1;
  else
    /* Ensure a stable sort.  */
    return DECL_UID (f1) - DECL_UID (f2);
}

/* Generate functions to call static constructors and destructors
   for targets that do not support .ctors/.dtors sections.  These
   functions have magic names which are detected by collect2.  */

static void
build_cdtor_fns (void)
{
  if (!static_ctors.is_empty ())
    {
      gcc_assert (!targetm.have_ctors_dtors || in_lto_p);
      static_ctors.qsort (compare_ctor);
      build_cdtor (/*ctor_p=*/true, static_ctors);
    }

  if (!static_dtors.is_empty ())
    {
      gcc_assert (!targetm.have_ctors_dtors || in_lto_p);
      static_dtors.qsort (compare_dtor);
      build_cdtor (/*ctor_p=*/false, static_dtors);
    }
}

/* Look for constructors and destructors and produce function calling them.
   This is needed for targets not supporting ctors or dtors, but we perform the
   transformation also at linktime to merge possibly numerous
   constructors/destructors into single function to improve code locality and
   reduce size.  */

static unsigned int
ipa_cdtor_merge (void)
{
  struct cgraph_node *node;
  FOR_EACH_DEFINED_FUNCTION (node)
    if (DECL_STATIC_CONSTRUCTOR (node->decl)
	|| DECL_STATIC_DESTRUCTOR (node->decl))
       record_cdtor_fn (node);
  build_cdtor_fns ();
  static_ctors.release ();
  static_dtors.release ();
  return 0;
}

namespace {

const pass_data pass_data_ipa_cdtor_merge =
{
  IPA_PASS, /* type */
  "cdtor", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  true, /* has_execute */
  TV_CGRAPHOPT, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_ipa_cdtor_merge : public ipa_opt_pass_d
{
public:
  pass_ipa_cdtor_merge (gcc::context *ctxt)
    : ipa_opt_pass_d (pass_data_ipa_cdtor_merge, ctxt,
		      NULL, /* generate_summary */
		      NULL, /* write_summary */
		      NULL, /* read_summary */
		      NULL, /* write_optimization_summary */
		      NULL, /* read_optimization_summary */
		      NULL, /* stmt_fixup */
		      0, /* function_transform_todo_flags_start */
		      NULL, /* function_transform */
		      NULL) /* variable_transform */
  {}

  /* opt_pass methods: */
  virtual bool gate (function *);
  virtual unsigned int execute (function *) { return ipa_cdtor_merge (); }

}; // class pass_ipa_cdtor_merge

bool
pass_ipa_cdtor_merge::gate (function *)
{
  /* Perform the pass when we have no ctors/dtors support
     or at LTO time to merge multiple constructors into single
     function.  */
  return !targetm.have_ctors_dtors || (optimize && in_lto_p);
}

} // anon namespace

ipa_opt_pass_d *
make_pass_ipa_cdtor_merge (gcc::context *ctxt)
{
  return new pass_ipa_cdtor_merge (ctxt);
}
