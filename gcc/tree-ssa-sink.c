/* Code sinking for trees
   Copyright (C) 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dan@dberlin.org>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-inline.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-dump.h"
#include "timevar.h"
#include "fibheap.h"
#include "hashtab.h"
#include "tree-iterator.h"
#include "real.h"
#include "alloc-pool.h"
#include "tree-pass.h"
#include "flags.h"
#include "bitmap.h"
#include "langhooks.h"
#include "cfgloop.h"

/* TODO:
   1. Sinking store only using scalar promotion (IE without moving the RHS):

   *q = p;
   p = p + 1;
   if (something)
     *q = <not p>;
   else
     y = *q;

   
   should become
   sinktemp = p;
   p = p + 1;
   if (something)
     *q = <not p>;
   else
   {
     *q = sinktemp;
     y = *q
   }
   Store copy propagation will take care of the store elimination above.
     

   2. Sinking using Partial Dead Code Elimination.  */


static struct
{  
  /* The number of statements sunk down the flowgraph by code sinking.  */
  int sunk;
  
} sink_stats;


/* Given a PHI, and one of its arguments (DEF), find the edge for
   that argument and return it.  If the argument occurs twice in the PHI node,
   we return NULL.  */

static basic_block
find_bb_for_arg (tree phi, tree def)
{
  int i;
  bool foundone = false;
  basic_block result = NULL;
  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
    if (PHI_ARG_DEF (phi, i) == def)
      {
	if (foundone)
	  return NULL;
	foundone = true;
	result = PHI_ARG_EDGE (phi, i)->src;
      }
  return result;
}

/* When the first immediate use is in a statement, then return true if all
   immediate uses in IMM are in the same statement.
   We could also do the case where  the first immediate use is in a phi node,
   and all the other uses are in phis in the same basic block, but this
   requires some expensive checking later (you have to make sure no def/vdef
   in the statement occurs for multiple edges in the various phi nodes it's
   used in, so that you only have one place you can sink it to.  */

static bool
all_immediate_uses_same_place (tree stmt)
{
  tree firstuse = NULL_TREE;
  ssa_op_iter op_iter;
  imm_use_iterator imm_iter;
  use_operand_p use_p;
  tree var;

  FOR_EACH_SSA_TREE_OPERAND (var, stmt, op_iter, SSA_OP_ALL_DEFS)
    {
      FOR_EACH_IMM_USE_FAST (use_p, imm_iter, var)
        {
	  if (firstuse == NULL_TREE)
	    firstuse = USE_STMT (use_p);
	  else
	    if (firstuse != USE_STMT (use_p))
	      return false;
	}
    }

  return true;
}

/* Some global stores don't necessarily have V_MAY_DEF's of global variables,
   but we still must avoid moving them around.  */

bool
is_hidden_global_store (tree stmt)
{
  stmt_ann_t ann = stmt_ann (stmt);
  v_may_def_optype v_may_defs;
  v_must_def_optype v_must_defs;
    
  /* Check virtual definitions.  If we get here, the only virtual
     definitions we should see are those generated by assignment
     statements.  */
  v_may_defs = V_MAY_DEF_OPS (ann);
  v_must_defs = V_MUST_DEF_OPS (ann);
  if (NUM_V_MAY_DEFS (v_may_defs) > 0 || NUM_V_MUST_DEFS (v_must_defs) > 0)
    {
      tree lhs;

      gcc_assert (TREE_CODE (stmt) == MODIFY_EXPR);

      /* Note that we must not check the individual virtual operands
	 here.  In particular, if this is an aliased store, we could
	 end up with something like the following (SSA notation
	 redacted for brevity):

	 	foo (int *p, int i)
		{
		  int x;
		  p_1 = (i_2 > 3) ? &x : p;

		  # x_4 = V_MAY_DEF <x_3>
		  *p_1 = 5;

		  return 2;
		}

	 Notice that the store to '*p_1' should be preserved, if we
	 were to check the virtual definitions in that store, we would
	 not mark it needed.  This is because 'x' is not a global
	 variable.

	 Therefore, we check the base address of the LHS.  If the
	 address is a pointer, we check if its name tag or type tag is
	 a global variable.  Otherwise, we check if the base variable
	 is a global.  */
      lhs = TREE_OPERAND (stmt, 0);
      if (REFERENCE_CLASS_P (lhs))
	lhs = get_base_address (lhs);

      if (lhs == NULL_TREE)
	{
	  /* If LHS is NULL, it means that we couldn't get the base
	     address of the reference.  In which case, we should not
	     move this store.  */
	  return true;
	}
      else if (DECL_P (lhs))
	{
	  /* If the store is to a global symbol, we need to keep it.  */
	  if (is_global_var (lhs))
	    return true;

	}
      else if (INDIRECT_REF_P (lhs))
	{
	  tree ptr = TREE_OPERAND (lhs, 0);
	  struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
	  tree nmt = (pi) ? pi->name_mem_tag : NULL_TREE;
	  tree tmt = var_ann (SSA_NAME_VAR (ptr))->type_mem_tag;

	  /* If either the name tag or the type tag for PTR is a
	     global variable, then the store is necessary.  */
	  if ((nmt && is_global_var (nmt))
	      || (tmt && is_global_var (tmt)))
	    {
	      return true;
	    }
	}
      else
	gcc_unreachable ();
    }
  return false;
}

/* Find the nearest common dominator of all of the immediate uses in IMM.  */

static basic_block
nearest_common_dominator_of_uses (tree stmt)
{  
  bitmap blocks = BITMAP_ALLOC (NULL);
  basic_block commondom;
  unsigned int j;
  bitmap_iterator bi;
  ssa_op_iter op_iter;
  imm_use_iterator imm_iter;
  use_operand_p use_p;
  tree var;

  bitmap_clear (blocks);
  FOR_EACH_SSA_TREE_OPERAND (var, stmt, op_iter, SSA_OP_ALL_DEFS)
    {
      FOR_EACH_IMM_USE_FAST (use_p, imm_iter, var)
        {
	  tree usestmt = USE_STMT (use_p);
	  basic_block useblock;
	  if (TREE_CODE (usestmt) == PHI_NODE)
	    {
	      int idx = PHI_ARG_INDEX_FROM_USE (use_p);
	      if (PHI_ARG_DEF (usestmt, idx) == var)
		{
		  useblock = PHI_ARG_EDGE (usestmt, idx)->src;
		  /* Short circuit. Nothing dominates the entry block.  */
		  if (useblock == ENTRY_BLOCK_PTR)
		    {
		      BITMAP_FREE (blocks);
		      return NULL;
		    }
		  bitmap_set_bit (blocks, useblock->index);
		}
	    }
	  else
	    {
	      useblock = bb_for_stmt (usestmt);

	      /* Short circuit. Nothing dominates the entry block.  */
	      if (useblock == ENTRY_BLOCK_PTR)
		{
		  BITMAP_FREE (blocks);
		  return NULL;
		}
	      bitmap_set_bit (blocks, useblock->index);
	    }
	}
    }
  commondom = BASIC_BLOCK (bitmap_first_set_bit (blocks));
  EXECUTE_IF_SET_IN_BITMAP (blocks, 0, j, bi)
    commondom = nearest_common_dominator (CDI_DOMINATORS, commondom, 
					  BASIC_BLOCK (j));
  BITMAP_FREE (blocks);
  return commondom;
}

/* Given a statement (STMT) and the basic block it is currently in (FROMBB), 
   determine the location to sink the statement to, if any.
   Return the basic block to sink it to, or NULL if we should not sink
   it.  */

static tree
statement_sink_location (tree stmt, basic_block frombb)
{
  tree use, def;
  use_operand_p one_use = NULL_USE_OPERAND_P;
  basic_block sinkbb;
  use_operand_p use_p;
  def_operand_p def_p;
  ssa_op_iter iter;
  stmt_ann_t ann;
  tree rhs;
  imm_use_iterator imm_iter;

  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_ALL_DEFS)
    {
      FOR_EACH_IMM_USE_FAST (one_use, imm_iter, def)
	{
	  break;
	}
      if (one_use != NULL_USE_OPERAND_P)
        break;
    }

  /* Return if there are no immediate uses of this stmt.  */
  if (one_use == NULL_USE_OPERAND_P)
    return NULL;

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return NULL;
  rhs = TREE_OPERAND (stmt, 1);

  /* There are a few classes of things we can't or don't move, some because we
     don't have code to handle it, some because it's not profitable and some
     because it's not legal. 
  
     We can't sink things that may be global stores, at least not without
     calculating a lot more information, because we may cause it to no longer
     be seen by an external routine that needs it depending on where it gets
     moved to.  
      
     We don't want to sink loads from memory.

     We can't sink statements that end basic blocks without splitting the
     incoming edge for the sink location to place it there.

     We can't sink statements that have volatile operands.  

     We don't want to sink dead code, so anything with 0 immediate uses is not
     sunk.  

  */
  ann = stmt_ann (stmt);
  if (NUM_VUSES (STMT_VUSE_OPS (stmt)) != 0
      || stmt_ends_bb_p (stmt)
      || TREE_SIDE_EFFECTS (rhs)
      || TREE_CODE (rhs) == EXC_PTR_EXPR
      || TREE_CODE (rhs) == FILTER_EXPR
      || is_hidden_global_store (stmt)
      || ann->has_volatile_ops)
    return NULL;
  
  FOR_EACH_SSA_DEF_OPERAND (def_p, stmt, iter, SSA_OP_ALL_DEFS)
    {
      tree def = DEF_FROM_PTR (def_p);
      if (is_global_var (SSA_NAME_VAR (def))
	  || SSA_NAME_OCCURS_IN_ABNORMAL_PHI (def))
	return NULL;
    }
    
  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_ALL_USES)
    {
      tree use = USE_FROM_PTR (use_p);
      if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (use))
	return NULL;
    }
  
  /* If all the immediate uses are not in the same place, find the nearest
     common dominator of all the immediate uses.  For PHI nodes, we have to
     find the nearest common dominator of all of the predecessor blocks, since
     that is where insertion would have to take place.  */
  if (!all_immediate_uses_same_place (stmt))
    {
      basic_block commondom = nearest_common_dominator_of_uses (stmt);
     
      if (commondom == frombb)
	return NULL;

      /* Our common dominator has to be dominated by frombb in order to be a
	 trivially safe place to put this statement, since it has multiple
	 uses.  */     
      if (!dominated_by_p (CDI_DOMINATORS, commondom, frombb))
	return NULL;
      
      /* It doesn't make sense to move to a dominator that post-dominates
	 frombb, because it means we've just moved it into a path that always
	 executes if frombb executes, instead of reducing the number of
	 executions .  */
      if (dominated_by_p (CDI_POST_DOMINATORS, frombb, commondom))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Not moving store, common dominator post-dominates from block.\n");
	  return NULL;
	}

      if (commondom == frombb || commondom->loop_depth > frombb->loop_depth)
	return NULL;
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Common dominator of all uses is %d\n",
		   commondom->index);
	}
      return first_stmt (commondom);
    }

  use = USE_STMT (one_use);
  if (TREE_CODE (use) != PHI_NODE)
    {
      sinkbb = bb_for_stmt (use);
      if (sinkbb == frombb || sinkbb->loop_depth > frombb->loop_depth
	  || sinkbb->loop_father != frombb->loop_father)
	return NULL;      
      return use;
    }

  /* Note that at this point, all uses must be in the same statement, so it
     doesn't matter which def op we choose.  */
  if (STMT_DEF_OPS (stmt) == NULL)
    {
      if (STMT_V_MAY_DEF_OPS (stmt) != NULL)
	def = V_MAY_DEF_RESULT (STMT_V_MAY_DEF_OPS (stmt), 0);
      else if (STMT_V_MUST_DEF_OPS (stmt) != NULL)
	def = V_MUST_DEF_RESULT (STMT_V_MUST_DEF_OPS (stmt), 0);
      else
	gcc_unreachable ();
    }
  else
    def = DEF_OP (STMT_DEF_OPS (stmt), 0);
  
  sinkbb = find_bb_for_arg (use, def);
  if (!sinkbb)
    return NULL;

  /* This will happen when you have
     a_3 = PHI <a_13, a_26>
       
     a_26 = V_MAY_DEF <a_3> 

     If the use is a phi, and is in the same bb as the def, 
     we can't sink it.  */

  if (bb_for_stmt (use) == frombb)
    return NULL;
  if (sinkbb == frombb || sinkbb->loop_depth > frombb->loop_depth
      || sinkbb->loop_father != frombb->loop_father)
    return NULL;

  return first_stmt (sinkbb);
}

/* Perform code sinking on BB */

static void
sink_code_in_bb (basic_block bb)
{
  basic_block son;
  block_stmt_iterator bsi;
  edge_iterator ei;
  edge e;
  
  /* If this block doesn't dominate anything, there can't be any place to sink
     the statements to.  */
  if (first_dom_son (CDI_DOMINATORS, bb) == NULL)
    goto earlyout;

  /* We can't move things across abnormal edges, so don't try.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->flags & EDGE_ABNORMAL)
      goto earlyout;

  for (bsi = bsi_last (bb); !bsi_end_p (bsi);)
    {
      tree stmt = bsi_stmt (bsi);	
      block_stmt_iterator tobsi;
      tree sinkstmt;
      get_stmt_operands (stmt);
      
      sinkstmt = statement_sink_location (stmt, bb);
      if (!sinkstmt)
	{
	  if (!bsi_end_p (bsi))
	    bsi_prev (&bsi);
	  continue;
	}      
      if (dump_file)
	{
	  fprintf (dump_file, "Sinking ");
	  print_generic_expr (dump_file, stmt, TDF_VOPS);
	  fprintf (dump_file, " from bb %d to bb %d\n",
		   bb->index, bb_for_stmt (sinkstmt)->index);
	}
      tobsi = bsi_for_stmt (sinkstmt);
      /* Find the first non-label.  */
      while (!bsi_end_p (tobsi)
             && TREE_CODE (bsi_stmt (tobsi)) == LABEL_EXPR)
        bsi_next (&tobsi);
      
      /* If this is the end of the basic block, we need to insert at the end
         of the basic block.  */
      if (bsi_end_p (tobsi))
	bsi_move_to_bb_end (&bsi, bb_for_stmt (sinkstmt));
      else
	bsi_move_before (&bsi, &tobsi);

      sink_stats.sunk++;
      if (!bsi_end_p (bsi))
	bsi_prev (&bsi);
      
    }
 earlyout:
  for (son = first_dom_son (CDI_POST_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_POST_DOMINATORS, son))
    {
      sink_code_in_bb (son);
    }
}  

/* Perform code sinking.
   This moves code down the flowgraph when we know it would be
   profitable to do so, or it wouldn't increase the number of
   executions of the statement.

   IE given
   
   a_1 = b + c;
   if (<something>)
   {
   }
   else
   {
     foo (&b, &c);
     a_5 = b + c;
   }
   a_6 = PHI (a_5, a_1);
   USE a_6.

   we'll transform this into:

   if (<something>)
   {
      a_1 = b + c;
   }
   else
   {
      foo (&b, &c);
      a_5 = b + c;
   }
   a_6 = PHI (a_5, a_1);
   USE a_6.

   Note that this reduces the number of computations of a = b + c to 1
   when we take the else edge, instead of 2.
*/
static void
execute_sink_code (void)
{
  struct loops *loops = loop_optimizer_init (dump_file);
  connect_infinite_loops_to_exit ();
  memset (&sink_stats, 0, sizeof (sink_stats));
  calculate_dominance_info (CDI_DOMINATORS | CDI_POST_DOMINATORS);
  sink_code_in_bb (EXIT_BLOCK_PTR); 
  if (dump_file && (dump_flags & TDF_STATS))
    fprintf (dump_file, "Sunk statements:%d\n", sink_stats.sunk);
  free_dominance_info (CDI_POST_DOMINATORS);
  remove_fake_exit_edges ();
  loop_optimizer_finalize (loops, dump_file);
}

/* Gate and execute functions for PRE.  */

static void
do_sink (void)
{
  execute_sink_code ();
}

static bool
gate_sink (void)
{
  return flag_tree_sink != 0;
}

struct tree_opt_pass pass_sink_code =
{
  "sink",				/* name */
  gate_sink,				/* gate */
  do_sink,				/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_SINK,				/* tv_id */
  PROP_no_crit_edges | PROP_cfg
    | PROP_ssa | PROP_alias,		/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_rename_vars | TODO_dump_func | TODO_ggc_collect | TODO_verify_ssa, /* todo_flags_finish */
  0					/* letter */
};
