/* Translation of ISL AST to Gimple.
   Copyright (C) 2014 Free Software Foundation, Inc.
   Contributed by Roman Gareev <gareevroman@gmail.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"

#ifdef HAVE_cloog
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_map.h>
#include <isl/ast_build.h>
#if defined(__cplusplus)
extern "C" {
#endif
#include <isl/val_gmp.h>
#if defined(__cplusplus)
}
#endif
#endif

#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "basic-block.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "tree-ssa-loop.h"
#include "tree-pass.h"
#include "cfgloop.h"
#include "tree-data-ref.h"
#include "sese.h"
#include "tree-ssa-loop-manip.h"
#include "tree-scalar-evolution.h"
#include <map>

#ifdef HAVE_cloog
#include "graphite-poly.h"
#include "graphite-isl-ast-to-gimple.h"

/* This flag is set when an error occurred during the translation of
   ISL AST to Gimple.  */

static bool graphite_regenerate_error;

/* We always use signed 128, until isl is able to give information about
types  */

static tree *graphite_expression_size_type = &int128_integer_type_node;

/* Converts a GMP constant VAL to a tree and returns it.  */

static tree
gmp_cst_to_tree (tree type, mpz_t val)
{
  tree t = type ? type : integer_type_node;
  mpz_t tmp;

  mpz_init (tmp);
  mpz_set (tmp, val);
  wide_int wi = wi::from_mpz (t, tmp, true);
  mpz_clear (tmp);

  return wide_int_to_tree (t, wi);
}

/* Verifies properties that GRAPHITE should maintain during translation.  */

static inline void
graphite_verify (void)
{
#ifdef ENABLE_CHECKING
  verify_loop_structure ();
  verify_loop_closed_ssa (true);
#endif
}

/* IVS_PARAMS maps ISL's scattering and parameter identifiers
   to corresponding trees.  */

typedef std::map<isl_id *, tree> ivs_params;

/* Free all memory allocated for ISL's identifiers.  */

void ivs_params_clear (ivs_params &ip)
{
  std::map<isl_id *, tree>::iterator it;
  for (it = ip.begin ();
       it != ip.end (); it++)
    {
      isl_id_free (it->first);
    }
}

static tree
gcc_expression_from_isl_expression (tree type, __isl_take isl_ast_expr *,
				    ivs_params &ip);

/* Return the tree variable that corresponds to the given isl ast identifier
 expression (an isl_ast_expr of type isl_ast_expr_id).  */

static tree
gcc_expression_from_isl_ast_expr_id (__isl_keep isl_ast_expr *expr_id,
				     ivs_params &ip)
{
  gcc_assert (isl_ast_expr_get_type (expr_id) == isl_ast_expr_id);
  isl_id *tmp_isl_id = isl_ast_expr_get_id (expr_id);
  std::map<isl_id *, tree>::iterator res;
  res = ip.find (tmp_isl_id);
  isl_id_free (tmp_isl_id);
  gcc_assert (res != ip.end () &&
              "Could not map isl_id to tree expression");
  isl_ast_expr_free (expr_id);
  return res->second;
}

/* Converts an isl_ast_expr_int expression E to a GCC expression tree of
   type TYPE.  */

static tree
gcc_expression_from_isl_expr_int (tree type, __isl_take isl_ast_expr *expr)
{
  gcc_assert (isl_ast_expr_get_type (expr) == isl_ast_expr_int);
  isl_val *val = isl_ast_expr_get_val (expr);
  mpz_t val_mpz_t;
  mpz_init (val_mpz_t);
  tree res;
  if (isl_val_get_num_gmp (val, val_mpz_t) == -1)
    res = NULL_TREE;
  else
    res = gmp_cst_to_tree (type, val_mpz_t);
  isl_val_free (val);
  isl_ast_expr_free (expr);
  mpz_clear (val_mpz_t);
  return res;
}

/* Converts a binary isl_ast_expr_op expression E to a GCC expression tree of
   type TYPE.  */

static tree
binary_op_to_tree (tree type, __isl_take isl_ast_expr *expr, ivs_params &ip)
{
  isl_ast_expr *arg_expr = isl_ast_expr_get_op_arg (expr, 0);
  tree tree_lhs_expr = gcc_expression_from_isl_expression (type, arg_expr, ip);
  arg_expr = isl_ast_expr_get_op_arg (expr, 1);
  tree tree_rhs_expr = gcc_expression_from_isl_expression (type, arg_expr, ip);
  enum isl_ast_op_type expr_type = isl_ast_expr_get_op_type (expr);
  isl_ast_expr_free (expr);
  switch (expr_type)
    {
    case isl_ast_op_add:
      return fold_build2 (PLUS_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_sub:
      return fold_build2 (MINUS_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_mul:
      return fold_build2 (MULT_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_div:
      return fold_build2 (EXACT_DIV_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_fdiv_q:
      return fold_build2 (FLOOR_DIV_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_and:
      return fold_build2 (TRUTH_ANDIF_EXPR, type,
			  tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_or:
      return fold_build2 (TRUTH_ORIF_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_eq:
      return fold_build2 (EQ_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_le:
      return fold_build2 (LE_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_lt:
      return fold_build2 (LT_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_ge:
      return fold_build2 (GE_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    case isl_ast_op_gt:
      return fold_build2 (GT_EXPR, type, tree_lhs_expr, tree_rhs_expr);

    default:
      gcc_unreachable ();
    }
}

/* Converts a ternary isl_ast_expr_op expression E to a GCC expression tree of
   type TYPE.  */

static tree
ternary_op_to_tree (tree type, __isl_take isl_ast_expr *expr, ivs_params &ip)
{
  gcc_assert (isl_ast_expr_get_op_type (expr) == isl_ast_op_minus);
  isl_ast_expr *arg_expr = isl_ast_expr_get_op_arg (expr, 0);
  tree tree_first_expr
    = gcc_expression_from_isl_expression (type, arg_expr, ip);
  arg_expr = isl_ast_expr_get_op_arg (expr, 1);
  tree tree_second_expr
    = gcc_expression_from_isl_expression (type, arg_expr, ip);
  arg_expr = isl_ast_expr_get_op_arg (expr, 2);
  tree tree_third_expr
    = gcc_expression_from_isl_expression (type, arg_expr, ip);
  isl_ast_expr_free (expr);
  return fold_build3 (COND_EXPR, type, tree_first_expr,
		      tree_second_expr, tree_third_expr);
}

/* Converts a unary isl_ast_expr_op expression E to a GCC expression tree of
   type TYPE.  */

static tree
unary_op_to_tree (tree type, __isl_take isl_ast_expr *expr, ivs_params &ip)
{
  gcc_assert (isl_ast_expr_get_op_type (expr) == isl_ast_op_minus);
  isl_ast_expr *arg_expr = isl_ast_expr_get_op_arg (expr, 0);
  tree tree_expr = gcc_expression_from_isl_expression (type, arg_expr, ip);
  isl_ast_expr_free (expr);
  return fold_build1 (NEGATE_EXPR, type, tree_expr);
}

/* Converts an isl_ast_expr_op expression E with unknown number of arguments
   to a GCC expression tree of type TYPE.  */

static tree
nary_op_to_tree (tree type, __isl_take isl_ast_expr *expr, ivs_params &ip)
{
  enum tree_code op_code;
  switch (isl_ast_expr_get_op_type (expr))
    {
    case isl_ast_op_max:
      op_code = MAX_EXPR;
      break;

    case isl_ast_op_min:
      op_code = MIN_EXPR;
      break;

    default:
      gcc_unreachable ();    
    }
  isl_ast_expr *arg_expr = isl_ast_expr_get_op_arg (expr, 0);
  tree res = gcc_expression_from_isl_expression (type, arg_expr, ip);
  int i;
  for (i = 1; i < isl_ast_expr_get_op_n_arg (expr); i++)
    {
      arg_expr = isl_ast_expr_get_op_arg (expr, i);
      tree t = gcc_expression_from_isl_expression (type, arg_expr, ip);
      res = fold_build2 (op_code, type, res, t);
    }
  isl_ast_expr_free (expr);
  return res;
}


/* Converts an isl_ast_expr_op expression E to a GCC expression tree of
   type TYPE.  */

static tree
gcc_expression_from_isl_expr_op (tree type, __isl_take isl_ast_expr *expr,
				 ivs_params &ip)
{
  gcc_assert (isl_ast_expr_get_type (expr) == isl_ast_expr_op);
  switch (isl_ast_expr_get_op_type (expr))
    {
    /* These isl ast expressions are not supported yet.  */
    case isl_ast_op_error:
    case isl_ast_op_call:
    case isl_ast_op_and_then:
    case isl_ast_op_or_else:
    case isl_ast_op_pdiv_q:
    case isl_ast_op_pdiv_r:
    case isl_ast_op_select:
      gcc_unreachable ();

    case isl_ast_op_max:
    case isl_ast_op_min:
      return nary_op_to_tree (type, expr, ip);

    case isl_ast_op_add:
    case isl_ast_op_sub:
    case isl_ast_op_mul:
    case isl_ast_op_div:
    case isl_ast_op_fdiv_q:
    case isl_ast_op_and:
    case isl_ast_op_or:
    case isl_ast_op_eq:
    case isl_ast_op_le:
    case isl_ast_op_lt:
    case isl_ast_op_ge:
    case isl_ast_op_gt:
      return binary_op_to_tree (type, expr, ip);

    case isl_ast_op_minus:
      return unary_op_to_tree (type, expr, ip);

    case isl_ast_op_cond:
      return ternary_op_to_tree (type, expr, ip);

    default:
      gcc_unreachable ();
    }

  return NULL_TREE;
}

/* Converts an ISL AST expression E back to a GCC expression tree of
   type TYPE.  */

static tree
gcc_expression_from_isl_expression (tree type, __isl_take isl_ast_expr *expr,
				    ivs_params &ip)
{
  switch (isl_ast_expr_get_type (expr))
    {
    case isl_ast_expr_id:
      return gcc_expression_from_isl_ast_expr_id (expr, ip);

    case isl_ast_expr_int:
      return gcc_expression_from_isl_expr_int (type, expr);

    case isl_ast_expr_op:
      return gcc_expression_from_isl_expr_op (type, expr, ip);

    default:
      gcc_unreachable ();
    }

  return NULL_TREE;
}

/* Creates a new LOOP corresponding to isl_ast_node_for.  Inserts an
   induction variable for the new LOOP.  New LOOP is attached to CFG
   starting at ENTRY_EDGE.  LOOP is inserted into the loop tree and
   becomes the child loop of the OUTER_LOOP.  NEWIVS_INDEX binds
   ISL's scattering name to the induction variable created for the
   loop of STMT.  The new induction variable is inserted in the NEWIVS
   vector and is of type TYPE.  */

static struct loop *
graphite_create_new_loop (edge entry_edge, __isl_keep isl_ast_node *node_for,
			  loop_p outer, tree type, tree lb, tree ub,
			  ivs_params &ip)
{
  isl_ast_expr *for_inc = isl_ast_node_for_get_inc (node_for);
  tree stride = gcc_expression_from_isl_expression (type, for_inc, ip);
  tree ivvar = create_tmp_var (type, "graphite_IV");
  tree iv, iv_after_increment;
  loop_p loop = create_empty_loop_on_edge
    (entry_edge, lb, stride, ub, ivvar, &iv, &iv_after_increment,
     outer ? outer : entry_edge->src->loop_father);

  isl_ast_expr *for_iterator = isl_ast_node_for_get_iterator (node_for);
  isl_id *id = isl_ast_expr_get_id (for_iterator);
  ip[id] = iv;
  isl_ast_expr_free (for_iterator);
  return loop;
}

static edge
translate_isl_ast (loop_p context_loop, __isl_keep isl_ast_node *node,
		   edge next_e, ivs_params &ip);

/* Create the loop for a isl_ast_node_for.

   - NEXT_E is the edge where new generated code should be attached.  */

static edge
translate_isl_ast_for_loop (loop_p context_loop,
			    __isl_keep isl_ast_node *node_for, edge next_e,
			    tree type, tree lb, tree ub,
			    ivs_params &ip)
{
  gcc_assert (isl_ast_node_get_type (node_for) == isl_ast_node_for);
  struct loop *loop = graphite_create_new_loop (next_e, node_for, context_loop,
						type, lb, ub, ip);
  edge last_e = single_exit (loop);
  edge to_body = single_succ_edge (loop->header);
  basic_block after = to_body->dest;

  /* Create a basic block for loop close phi nodes.  */
  last_e = single_succ_edge (split_edge (last_e));

  /* Translate the body of the loop.  */
  isl_ast_node *for_body = isl_ast_node_for_get_body (node_for);
  next_e = translate_isl_ast (loop, for_body, to_body, ip);
  isl_ast_node_free (for_body);
  redirect_edge_succ_nodup (next_e, after);
  set_immediate_dominator (CDI_DOMINATORS, next_e->dest, next_e->src);

  /* TODO: Add checking for the loop parallelism.  */

  return last_e;
}

/* We use this function to get the upper bound because of the form,
   which is used by isl to represent loops:

   for (iterator = init; cond; iterator += inc)

   {

     ...

   }

   The loop condition is an arbitrary expression, which contains the
   current loop iterator.

   (e.g. iterator + 3 < B && C > iterator + A)

   We have to know the upper bound of the iterator to generate a loop
   in Gimple form. It can be obtained from the special representation
   of the loop condition, which is generated by isl,
   if the ast_build_atomic_upper_bound option is set. In this case,
   isl generates a loop condition that consists of the current loop
   iterator, + an operator (< or <=) and an expression not involving
   the iterator, which is processed and returned by this function.

   (e.g iterator <= upper-bound-expression-without-iterator)  */

static __isl_give isl_ast_expr *
get_upper_bound (__isl_keep isl_ast_node *node_for)
{
  gcc_assert (isl_ast_node_get_type (node_for) == isl_ast_node_for);
  isl_ast_expr *for_cond = isl_ast_node_for_get_cond (node_for);
  gcc_assert (isl_ast_expr_get_type (for_cond) == isl_ast_expr_op);
  isl_ast_expr *res;
  switch (isl_ast_expr_get_op_type (for_cond))
    {
    case isl_ast_op_le:
      res = isl_ast_expr_get_op_arg (for_cond, 1);
      break;

    case isl_ast_op_lt:
      {
        // (iterator < ub) => (iterator <= ub - 1)
        isl_val *one = isl_val_int_from_si (isl_ast_expr_get_ctx (for_cond), 1);
        isl_ast_expr *ub = isl_ast_expr_get_op_arg (for_cond, 1);
        res = isl_ast_expr_sub (ub, isl_ast_expr_from_val (one));
        break;
      }

    default:
      gcc_unreachable ();
    }
  isl_ast_expr_free (for_cond);
  return res;
}

/* All loops generated by create_empty_loop_on_edge have the form of
   a post-test loop:

   do

   {
     body of the loop;
   } while (lower bound < upper bound);

   We create a new if region protecting the loop to be executed, if
   the execution count is zero (lower bound > upper bound).  */

static edge
graphite_create_new_loop_guard (edge entry_edge,
				__isl_keep isl_ast_node *node_for, tree *type,
				tree *lb, tree *ub, ivs_params &ip)
{
  gcc_assert (isl_ast_node_get_type (node_for) == isl_ast_node_for);
  tree cond_expr;
  edge exit_edge;

  *type = *graphite_expression_size_type;
  isl_ast_expr *for_init = isl_ast_node_for_get_init (node_for);
  *lb = gcc_expression_from_isl_expression (*type, for_init, ip);
  isl_ast_expr *upper_bound = get_upper_bound (node_for);
  *ub = gcc_expression_from_isl_expression (*type, upper_bound, ip);
  
  /* When ub is simply a constant or a parameter, use lb <= ub.  */
  if (TREE_CODE (*ub) == INTEGER_CST || TREE_CODE (*ub) == SSA_NAME)
    cond_expr = fold_build2 (LE_EXPR, boolean_type_node, *lb, *ub);
  else
    {
      tree one = (POINTER_TYPE_P (*type)
		  ? convert_to_ptrofftype (integer_one_node)
		  : fold_convert (*type, integer_one_node));
      /* Adding +1 and using LT_EXPR helps with loop latches that have a
	 loop iteration count of "PARAMETER - 1".  For PARAMETER == 0 this
	 becomes 2^k-1 due to integer overflow, and the condition lb <= ub
	 is true, even if we do not want this.  However lb < ub + 1 is false,
	 as expected.  */
      tree ub_one = fold_build2 (POINTER_TYPE_P (*type) ? POINTER_PLUS_EXPR
				 : PLUS_EXPR, *type, *ub, one);

      cond_expr = fold_build2 (LT_EXPR, boolean_type_node, *lb, ub_one);
    }

  exit_edge = create_empty_if_region_on_edge (entry_edge, cond_expr);

  return exit_edge;
}

/* Translates an isl_ast_node_for to Gimple. */

static edge
translate_isl_ast_node_for (loop_p context_loop, __isl_keep isl_ast_node *node,
			    edge next_e, ivs_params &ip)
{
  gcc_assert (isl_ast_node_get_type (node) == isl_ast_node_for);
  tree type, lb, ub;
  edge last_e = graphite_create_new_loop_guard (next_e, node, &type,
						&lb, &ub, ip);
  edge true_e = get_true_edge_from_guard_bb (next_e->dest);

  translate_isl_ast_for_loop (context_loop, node, true_e,
			      type, lb, ub, ip);
  return last_e;
}

/* Translates an ISL AST node NODE to GCC representation in the
   context of a SESE.  */

static edge
translate_isl_ast (loop_p context_loop, __isl_keep isl_ast_node *node,
		   edge next_e, ivs_params &ip)
{
  switch (isl_ast_node_get_type (node))
    {
    case isl_ast_node_error:
      gcc_unreachable ();

    case isl_ast_node_for:
      return translate_isl_ast_node_for (context_loop, node,
					 next_e, ip);

    case isl_ast_node_if:
      return next_e;

    case isl_ast_node_user:
      return next_e;

    case isl_ast_node_block:
      return next_e;

    default:
      gcc_unreachable ();
    }
}

/* Prints NODE to FILE.  */

void
print_isl_ast_node (FILE *file, __isl_keep isl_ast_node *node, 
		    __isl_keep isl_ctx *ctx)
{
  isl_printer *prn = isl_printer_to_file (ctx, file);
  prn = isl_printer_set_output_format (prn, ISL_FORMAT_C);
  prn = isl_printer_print_ast_node (prn, node);
  prn = isl_printer_print_str (prn, "\n");
  isl_printer_free (prn);
}

/* Add ISL's parameter identifiers and corresponding.trees to ivs_params  */

static void
add_parameters_to_ivs_params (scop_p scop, ivs_params &ip)
{
  sese region = SCOP_REGION (scop);
  unsigned nb_parameters = isl_set_dim (scop->context, isl_dim_param);
  gcc_assert (nb_parameters == SESE_PARAMS (region).length ());
  unsigned i;
  for (i = 0; i < nb_parameters; i++)
    {
      isl_id *tmp_id = isl_set_get_dim_id (scop->context, isl_dim_param, i);
      ip[tmp_id] = SESE_PARAMS (region)[i];
    }
}


/* Generates a build, which specifies the constraints on the parameters.  */

static __isl_give isl_ast_build *
generate_isl_context (scop_p scop)
{
  isl_set *context_isl = isl_set_params (isl_set_copy (scop->context));
  return isl_ast_build_from_context (context_isl);
}

/* Generates a schedule, which specifies an order used to
   visit elements in a domain.  */

static __isl_give isl_union_map *
generate_isl_schedule (scop_p scop)
{
  int i;
  poly_bb_p pbb;
  isl_union_map *schedule_isl =
    isl_union_map_empty (isl_set_get_space (scop->context));

  FOR_EACH_VEC_ELT (SCOP_BBS (scop), i, pbb)
    {
      /* Dead code elimination: when the domain of a PBB is empty,
	 don't generate code for the PBB.  */
      if (isl_set_is_empty (pbb->domain))
	continue;

      isl_map *bb_schedule = isl_map_copy (pbb->transformed);
      bb_schedule = isl_map_intersect_domain (bb_schedule,
					      isl_set_copy (pbb->domain));
      schedule_isl =
        isl_union_map_union (schedule_isl,
			     isl_union_map_from_map (bb_schedule));
    }
  return schedule_isl;
}

static __isl_give isl_ast_node *
scop_to_isl_ast (scop_p scop, ivs_params &ip)
{
  /* Generate loop upper bounds that consist of the current loop iterator,
  an operator (< or <=) and an expression not involving the iterator.
  If this option is not set, then the current loop iterator may appear several
  times in the upper bound. See the isl manual for more details.  */
  isl_options_set_ast_build_atomic_upper_bound (scop->ctx, true);

  add_parameters_to_ivs_params (scop, ip);
  isl_union_map *schedule_isl = generate_isl_schedule (scop);
  isl_ast_build *context_isl = generate_isl_context (scop);
  isl_ast_node *ast_isl = isl_ast_build_ast_from_schedule (context_isl,
							   schedule_isl);
  isl_ast_build_free (context_isl);
  return ast_isl;
}

/* GIMPLE Loop Generator: generates loops from STMT in GIMPLE form for
   the given SCOP.  Return true if code generation succeeded.

   FIXME: This is not yet a full implementation of the code generator
          with ISL ASTs. Generation of GIMPLE code has to be completed.  */

bool
graphite_regenerate_ast_isl (scop_p scop)
{
  loop_p context_loop;
  sese region = SCOP_REGION (scop);
  ifsese if_region = NULL;
  isl_ast_node *root_node;
  ivs_params ip;

  timevar_push (TV_GRAPHITE_CODE_GEN);
  graphite_regenerate_error = false;
  root_node = scop_to_isl_ast (scop, ip);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nISL AST generated by ISL: \n");
      print_isl_ast_node (dump_file, root_node, scop->ctx);
      fprintf (dump_file, "\n");
    }

  recompute_all_dominators ();
  graphite_verify ();

  if_region = move_sese_in_condition (region);
  sese_insert_phis_for_liveouts (region,
				 if_region->region->exit->src,
				 if_region->false_region->exit,
				 if_region->true_region->exit);
  recompute_all_dominators ();
  graphite_verify ();

  context_loop = SESE_ENTRY (region)->src->loop_father;

  translate_isl_ast (context_loop, root_node, if_region->true_region->entry,
		     ip);
  graphite_verify ();
  scev_reset ();
  recompute_all_dominators ();
  graphite_verify ();

  if (graphite_regenerate_error)
    set_ifsese_condition (if_region, integer_zero_node);

  free (if_region->true_region);
  free (if_region->region);
  free (if_region);

  ivs_params_clear (ip);
  isl_ast_node_free (root_node);
  timevar_pop (TV_GRAPHITE_CODE_GEN);
  /* TODO: Add dump  */
  return !graphite_regenerate_error;
}
#endif
