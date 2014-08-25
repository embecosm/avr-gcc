/* Subroutines used for code generation of Andes NDS32 cpu for GNU compiler
   Copyright (C) 2012-2014 Free Software Foundation, Inc.
   Contributed by Andes Technology Corporation.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

/* ------------------------------------------------------------------------ */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "stor-layout.h"
#include "varasm.h"
#include "calls.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "insn-config.h"	/* Required by recog.h.  */
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"		/* For DFA state_t.  */
#include "insn-codes.h"		/* For CODE_FOR_xxx.  */
#include "reload.h"		/* For push_reload().  */
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "recog.h"
#include "diagnostic-core.h"
#include "df.h"
#include "tm_p.h"
#include "tm-constrs.h"
#include "optabs.h"		/* For GEN_FCN.  */
#include "target.h"
#include "target-def.h"
#include "langhooks.h"		/* For add_builtin_function().  */
#include "ggc.h"
#include "builtins.h"

/* ------------------------------------------------------------------------ */

/* This file is divided into five parts:

     PART 1: Auxiliary static variable definitions and
             target hook static variable definitions.

     PART 2: Auxiliary static function definitions.

     PART 3: Implement target hook stuff definitions.

     PART 4: Implemet extern function definitions,
             the prototype is in nds32-protos.h.

     PART 5: Initialize target hook structure and definitions.  */

/* ------------------------------------------------------------------------ */

/* PART 1: Auxiliary static variable definitions and
           target hook static variable definitions.  */

/* Define intrinsic register names.
   Please refer to nds32_intrinsic.h file, the index is corresponding to
   'enum nds32_intrinsic_registers' data type values.
   NOTE that the base value starting from 1024.  */
static const char * const nds32_intrinsic_register_names[] =
{
  "$PSW", "$IPSW", "$ITYPE", "$IPC"
};

/* Defining target-specific uses of __attribute__.  */
static const struct attribute_spec nds32_attribute_table[] =
{
  /* Syntax: { name, min_len, max_len, decl_required, type_required,
               function_type_required, handler, affects_type_identity } */

  /* The interrupt vid: [0-63]+ (actual vector number starts from 9 to 72).  */
  { "interrupt",    1, 64, false, false, false, NULL, false },
  /* The exception vid: [1-8]+  (actual vector number starts from 1 to 8).  */
  { "exception",    1,  8, false, false, false, NULL, false },
  /* Argument is user's interrupt numbers.  The vector number is always 0.  */
  { "reset",        1,  1, false, false, false, NULL, false },

  /* The attributes describing isr nested type.  */
  { "nested",       0,  0, false, false, false, NULL, false },
  { "not_nested",   0,  0, false, false, false, NULL, false },
  { "nested_ready", 0,  0, false, false, false, NULL, false },

  /* The attributes describing isr register save scheme.  */
  { "save_all",     0,  0, false, false, false, NULL, false },
  { "partial_save", 0,  0, false, false, false, NULL, false },

  /* The attributes used by reset attribute.  */
  { "nmi",          1,  1, false, false, false, NULL, false },
  { "warm",         1,  1, false, false, false, NULL, false },

  /* The attribute telling no prologue/epilogue.  */
  { "naked",        0,  0, false, false, false, NULL, false },

  /* The last attribute spec is set to be NULL.  */
  { NULL,           0,  0, false, false, false, NULL, false }
};


/* ------------------------------------------------------------------------ */

/* PART 2: Auxiliary static function definitions.  */

/* Function to save and restore machine-specific function data.  */
static struct machine_function *
nds32_init_machine_status (void)
{
  struct machine_function *machine;
  machine = ggc_cleared_alloc<machine_function> ();

  /* Initially assume this function needs prologue/epilogue.  */
  machine->naked_p = 0;

  /* Initially assume this function does NOT use fp_as_gp optimization.  */
  machine->fp_as_gp_p = 0;

  return machine;
}

/* Function to compute stack frame size and
   store into cfun->machine structure.  */
static void
nds32_compute_stack_frame (void)
{
  int r;
  int block_size;

  /* Because nds32_compute_stack_frame() will be called from different place,
     everytime we enter this function, we have to assume this function
     needs prologue/epilogue.  */
  cfun->machine->naked_p = 0;

  /* Get variadic arguments size to prepare pretend arguments and
     push them into stack at prologue.
     Currently, we do not push variadic arguments by ourself.
     We have GCC handle all the works.
     The caller will push all corresponding nameless arguments into stack,
     and the callee is able to retrieve them without problems.
     These variables are still preserved in case one day
     we would like caller passing arguments with registers.  */
  cfun->machine->va_args_size = 0;
  cfun->machine->va_args_first_regno = SP_REGNUM;
  cfun->machine->va_args_last_regno  = SP_REGNUM;

  /* Get local variables, incoming variables, and temporary variables size.
     Note that we need to make sure it is 8-byte alignment because
     there may be no padding bytes if we are using LRA.  */
  cfun->machine->local_size = NDS32_ROUND_UP_DOUBLE_WORD (get_frame_size ());

  /* Get outgoing arguments size.  */
  cfun->machine->out_args_size = crtl->outgoing_args_size;

  /* If $fp value is required to be saved on stack, it needs 4 bytes space.
     Check whether $fp is ever live.  */
  cfun->machine->fp_size = (df_regs_ever_live_p (FP_REGNUM)) ? 4 : 0;

  /* If $gp value is required to be saved on stack, it needs 4 bytes space.
     Check whether we are using PIC code genration.  */
  cfun->machine->gp_size = (flag_pic) ? 4 : 0;

  /* If $lp value is required to be saved on stack, it needs 4 bytes space.
     Check whether $lp is ever live.  */
  cfun->machine->lp_size = (df_regs_ever_live_p (LP_REGNUM)) ? 4 : 0;

  /* Initially there is no padding bytes.  */
  cfun->machine->callee_saved_area_padding_bytes = 0;

  /* Calculate the bytes of saving callee-saved registers on stack.  */
  cfun->machine->callee_saved_regs_size = 0;
  cfun->machine->callee_saved_regs_first_regno = SP_REGNUM;
  cfun->machine->callee_saved_regs_last_regno  = SP_REGNUM;
  /* Currently, there is no need to check $r28~$r31
     because we will save them in another way.  */
  for (r = 0; r < 28; r++)
    {
      if (NDS32_REQUIRED_CALLEE_SAVED_P (r))
	{
	  /* Mark the first required callee-saved register
	     (only need to set it once).
	     If first regno == SP_REGNUM, we can tell that
	     it is the first time to be here.  */
	  if (cfun->machine->callee_saved_regs_first_regno == SP_REGNUM)
	    cfun->machine->callee_saved_regs_first_regno = r;
	  /* Mark the last required callee-saved register.  */
	  cfun->machine->callee_saved_regs_last_regno = r;
	}
    }

  /* Check if this function can omit prologue/epilogue code fragment.
     If there is 'naked' attribute in this function,
     we can set 'naked_p' flag to indicate that
     we do not have to generate prologue/epilogue.
     Or, if all the following conditions succeed,
     we can set this function 'naked_p' as well:
       condition 1: first_regno == last_regno == SP_REGNUM,
                    which means we do not have to save
                    any callee-saved registers.
       condition 2: Both $lp and $fp are NOT live in this function,
                    which means we do not need to save them.
       condition 3: There is no local_size, which means
                    we do not need to adjust $sp.  */
  if (lookup_attribute ("naked", DECL_ATTRIBUTES (current_function_decl))
      || (cfun->machine->callee_saved_regs_first_regno == SP_REGNUM
	  && cfun->machine->callee_saved_regs_last_regno == SP_REGNUM
	  && !df_regs_ever_live_p (FP_REGNUM)
	  && !df_regs_ever_live_p (LP_REGNUM)
	  && cfun->machine->local_size == 0))
    {
      /* Set this function 'naked_p' and
         other functions can check this flag.  */
      cfun->machine->naked_p = 1;

      /* No need to save $fp, $gp, and $lp.
         We should set these value to be zero
         so that nds32_initial_elimination_offset() can work properly.  */
      cfun->machine->fp_size = 0;
      cfun->machine->gp_size = 0;
      cfun->machine->lp_size = 0;

      /* If stack usage computation is required,
         we need to provide the static stack size.  */
      if (flag_stack_usage_info)
	current_function_static_stack_size = 0;

      /* No need to do following adjustment, return immediately.  */
      return;
    }

  /* Adjustment for v3push instructions:
     If we are using v3push (push25/pop25) instructions,
     we need to make sure Rb is $r6 and Re is
     located on $r6, $r8, $r10, or $r14.
     Some results above will be discarded and recomputed.
     Note that it is only available under V3/V3M ISA.  */
  if (TARGET_V3PUSH)
    {
      /* Recompute:
           cfun->machine->fp_size
           cfun->machine->gp_size
           cfun->machine->lp_size
           cfun->machine->callee_saved_regs_first_regno
           cfun->machine->callee_saved_regs_last_regno */

      /* For v3push instructions, $fp, $gp, and $lp are always saved.  */
      cfun->machine->fp_size = 4;
      cfun->machine->gp_size = 4;
      cfun->machine->lp_size = 4;

      /* Remember to set Rb = $r6.  */
      cfun->machine->callee_saved_regs_first_regno = 6;

      if (cfun->machine->callee_saved_regs_last_regno <= 6)
	{
	  /* Re = $r6 */
	  cfun->machine->callee_saved_regs_last_regno = 6;
	}
      else if (cfun->machine->callee_saved_regs_last_regno <= 8)
	{
	  /* Re = $r8 */
	  cfun->machine->callee_saved_regs_last_regno = 8;
	}
      else if (cfun->machine->callee_saved_regs_last_regno <= 10)
	{
	  /* Re = $r10 */
	  cfun->machine->callee_saved_regs_last_regno = 10;
	}
      else if (cfun->machine->callee_saved_regs_last_regno <= 14)
	{
	  /* Re = $r14 */
	  cfun->machine->callee_saved_regs_last_regno = 14;
	}
      else if (cfun->machine->callee_saved_regs_last_regno == SP_REGNUM)
	{
	  /* If last_regno is SP_REGNUM, which means
	     it is never changed, so set it to Re = $r6.  */
	  cfun->machine->callee_saved_regs_last_regno = 6;
	}
      else
	{
	  /* The program flow should not go here.  */
	  gcc_unreachable ();
	}
    }

  /* We have correctly set callee_saved_regs_first_regno
     and callee_saved_regs_last_regno.
     Initially, the callee_saved_regs_size is supposed to be 0.
     As long as callee_saved_regs_last_regno is not SP_REGNUM,
     we can update callee_saved_regs_size with new size.  */
  if (cfun->machine->callee_saved_regs_last_regno != SP_REGNUM)
    {
      /* Compute pushed size of callee-saved registers.  */
      cfun->machine->callee_saved_regs_size
	= 4 * (cfun->machine->callee_saved_regs_last_regno
	       - cfun->machine->callee_saved_regs_first_regno
	       + 1);
    }

  /* Important: We need to make sure that
                (va_args_size + fp_size + gp_size
                 + lp_size + callee_saved_regs_size)
                is 8-byte alignment.
                If it is not, calculate the padding bytes.  */
  block_size = cfun->machine->va_args_size
	       + cfun->machine->fp_size
	       + cfun->machine->gp_size
	       + cfun->machine->lp_size
	       + cfun->machine->callee_saved_regs_size;
  if (!NDS32_DOUBLE_WORD_ALIGN_P (block_size))
    {
      cfun->machine->callee_saved_area_padding_bytes
	= NDS32_ROUND_UP_DOUBLE_WORD (block_size) - block_size;
    }

  /* If stack usage computation is required,
     we need to provide the static stack size.  */
  if (flag_stack_usage_info)
    {
      current_function_static_stack_size
	= NDS32_ROUND_UP_DOUBLE_WORD (block_size)
	  + cfun->machine->local_size
	  + cfun->machine->out_args_size;
    }
}

/* Function to create a parallel rtx pattern
   which presents stack push multiple behavior.
   The overall concept are:
     "push registers to memory",
     "adjust stack pointer".  */
static rtx
nds32_gen_stack_push_multiple (rtx Rb, rtx Re,
			       rtx En4 ATTRIBUTE_UNUSED)
{
  int regno;
  int extra_count;
  int num_use_regs;
  int par_index;
  int offset;

  rtx reg;
  rtx mem;
  rtx push_rtx;
  rtx adjust_sp_rtx;
  rtx parallel_insn;

  /* We need to provide a customized rtx which contains
     necessary information for data analysis,
     so we create a parallel rtx like this:
     (parallel [(set (mem (plus (reg:SI SP_REGNUM) (const_int -32)))
                     (reg:SI Rb))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -28)))
                     (reg:SI Rb+1))
                ...
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -16)))
                     (reg:SI Re))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -12)))
                     (reg:SI FP_REGNUM))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -8)))
                     (reg:SI GP_REGNUM))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -4)))
                     (reg:SI LP_REGNUM))
                (set (reg:SI SP_REGNUM)
                     (plus (reg:SI SP_REGNUM) (const_int -32)))]) */

  /* Calculate the number of registers that will be pushed.  */
  extra_count = 0;
  if (cfun->machine->fp_size)
    extra_count++;
  if (cfun->machine->gp_size)
    extra_count++;
  if (cfun->machine->lp_size)
    extra_count++;
  /* Note that Rb and Re may be SP_REGNUM.  DO NOT count it in.  */
  if (REGNO (Rb) == SP_REGNUM && REGNO (Re) == SP_REGNUM)
    num_use_regs = extra_count;
  else
    num_use_regs = REGNO (Re) - REGNO (Rb) + 1 + extra_count;

  /* In addition to used registers,
     we need one more space for (set sp sp-x) rtx.  */
  parallel_insn = gen_rtx_PARALLEL (VOIDmode,
				    rtvec_alloc (num_use_regs + 1));
  par_index = 0;

  /* Initialize offset and start to create push behavior.  */
  offset = -(num_use_regs * 4);

  /* Create (set mem regX) from Rb, Rb+1 up to Re.  */
  for (regno = REGNO (Rb); regno <= (int) REGNO (Re); regno++)
    {
      /* Rb and Re may be SP_REGNUM.
         We need to break this loop immediately.  */
      if (regno == SP_REGNUM)
        break;

      reg = gen_rtx_REG (SImode, regno);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
      XVECEXP (parallel_insn, 0, par_index) = push_rtx;
      RTX_FRAME_RELATED_P (push_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }

  /* Create (set mem fp), (set mem gp), and (set mem lp) if necessary.  */
  if (cfun->machine->fp_size)
    {
      reg = gen_rtx_REG (SImode, FP_REGNUM);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
      XVECEXP (parallel_insn, 0, par_index) = push_rtx;
      RTX_FRAME_RELATED_P (push_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }
  if (cfun->machine->gp_size)
    {
      reg = gen_rtx_REG (SImode, GP_REGNUM);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
      XVECEXP (parallel_insn, 0, par_index) = push_rtx;
      RTX_FRAME_RELATED_P (push_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }
  if (cfun->machine->lp_size)
    {
      reg = gen_rtx_REG (SImode, LP_REGNUM);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
      XVECEXP (parallel_insn, 0, par_index) = push_rtx;
      RTX_FRAME_RELATED_P (push_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }

  /* Create (set sp sp-x).  */

  /* We need to re-calculate the offset value again for adjustment.  */
  offset = -(num_use_regs * 4);
  adjust_sp_rtx
    = gen_rtx_SET (VOIDmode,
		   stack_pointer_rtx,
		   plus_constant (Pmode, stack_pointer_rtx, offset));
  XVECEXP (parallel_insn, 0, par_index) = adjust_sp_rtx;
  RTX_FRAME_RELATED_P (adjust_sp_rtx) = 1;

  return parallel_insn;
}

/* Function to create a parallel rtx pattern
   which presents stack pop multiple behavior.
   The overall concept are:
     "pop registers from memory",
     "adjust stack pointer".  */
static rtx
nds32_gen_stack_pop_multiple (rtx Rb, rtx Re,
			      rtx En4 ATTRIBUTE_UNUSED)
{
  int regno;
  int extra_count;
  int num_use_regs;
  int par_index;
  int offset;

  rtx reg;
  rtx mem;
  rtx pop_rtx;
  rtx adjust_sp_rtx;
  rtx parallel_insn;

  /* We need to provide a customized rtx which contains
     necessary information for data analysis,
     so we create a parallel rtx like this:
     (parallel [(set (reg:SI Rb)
                     (mem (reg:SI SP_REGNUM)))
                (set (reg:SI Rb+1)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 4))))
                ...
                (set (reg:SI Re)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 16))))
                (set (reg:SI FP_REGNUM)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 20))))
                (set (reg:SI GP_REGNUM)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 24))))
                (set (reg:SI LP_REGNUM)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 28))))
                (set (reg:SI SP_REGNUM)
                     (plus (reg:SI SP_REGNUM) (const_int 32)))]) */

  /* Calculate the number of registers that will be poped.  */
  extra_count = 0;
  if (cfun->machine->fp_size)
    extra_count++;
  if (cfun->machine->gp_size)
    extra_count++;
  if (cfun->machine->lp_size)
    extra_count++;
  /* Note that Rb and Re may be SP_REGNUM.  DO NOT count it in.  */
  if (REGNO (Rb) == SP_REGNUM && REGNO (Re) == SP_REGNUM)
    num_use_regs = extra_count;
  else
    num_use_regs = REGNO (Re) - REGNO (Rb) + 1 + extra_count;

  /* In addition to used registers,
     we need one more space for (set sp sp+x) rtx.  */
  parallel_insn = gen_rtx_PARALLEL (VOIDmode,
				    rtvec_alloc (num_use_regs + 1));
  par_index = 0;

  /* Initialize offset and start to create pop behavior.  */
  offset = 0;

  /* Create (set regX mem) from Rb, Rb+1 up to Re.  */
  for (regno = REGNO (Rb); regno <= (int) REGNO (Re); regno++)
    {
      /* Rb and Re may be SP_REGNUM.
         We need to break this loop immediately.  */
      if (regno == SP_REGNUM)
        break;

      reg = gen_rtx_REG (SImode, regno);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
      XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
      RTX_FRAME_RELATED_P (pop_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }

  /* Create (set fp mem), (set gp mem), and (set lp mem) if necessary.  */
  if (cfun->machine->fp_size)
    {
      reg = gen_rtx_REG (SImode, FP_REGNUM);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
      XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
      RTX_FRAME_RELATED_P (pop_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }
  if (cfun->machine->gp_size)
    {
      reg = gen_rtx_REG (SImode, GP_REGNUM);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
      XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
      RTX_FRAME_RELATED_P (pop_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }
  if (cfun->machine->lp_size)
    {
      reg = gen_rtx_REG (SImode, LP_REGNUM);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
      XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
      RTX_FRAME_RELATED_P (pop_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }

  /* Create (set sp sp+x).  */

  /* The offset value is already in place.  No need to re-calculate it.  */
  adjust_sp_rtx
    = gen_rtx_SET (VOIDmode,
		   stack_pointer_rtx,
		   plus_constant (Pmode, stack_pointer_rtx, offset));
  XVECEXP (parallel_insn, 0, par_index) = adjust_sp_rtx;
  RTX_FRAME_RELATED_P (adjust_sp_rtx) = 1;

  return parallel_insn;
}

/* Function to create a parallel rtx pattern
   which presents stack v3push behavior.
   The overall concept are:
     "push registers to memory",
     "adjust stack pointer".  */
static rtx
nds32_gen_stack_v3push (rtx Rb,
			rtx Re,
			rtx En4 ATTRIBUTE_UNUSED,
			rtx imm8u)
{
  int regno;
  int num_use_regs;
  int par_index;
  int offset;

  rtx reg;
  rtx mem;
  rtx push_rtx;
  rtx adjust_sp_rtx;
  rtx parallel_insn;

  /* We need to provide a customized rtx which contains
     necessary information for data analysis,
     so we create a parallel rtx like this:
     (parallel [
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -32)))
                     (reg:SI Rb))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -28)))
                     (reg:SI Rb+1))
                ...
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -16)))
                     (reg:SI Re))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -12)))
                     (reg:SI FP_REGNUM))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -8)))
                     (reg:SI GP_REGNUM))
                (set (mem (plus (reg:SI SP_REGNUM) (const_int -4)))
                     (reg:SI LP_REGNUM))
                (set (reg:SI SP_REGNUM)
                     (plus (reg:SI SP_REGNUM) (const_int -32-imm8u)))]) */

  /* Calculate the number of registers that will be pushed.
     Since $fp, $gp, and $lp is always pushed with v3push instruction,
     we need to count these three registers.
     Under v3push, Rb is $r6, while Re is $r6, $r8, $r10, or $r14.
     So there is no need to worry about Rb=Re=SP_REGNUM case.  */
  num_use_regs = REGNO (Re) - REGNO (Rb) + 1 + 3;

  /* In addition to used registers,
     we need one more space for (set sp sp-x-imm8u) rtx.  */
  parallel_insn = gen_rtx_PARALLEL (VOIDmode,
				    rtvec_alloc (num_use_regs + 1));
  par_index = 0;

  /* Initialize offset and start to create push behavior.  */
  offset = -(num_use_regs * 4);

  /* Create (set mem regX) from Rb, Rb+1 up to Re.
     Under v3push, Rb is $r6, while Re is $r6, $r8, $r10, or $r14.
     So there is no need to worry about Rb=Re=SP_REGNUM case.  */
  for (regno = REGNO (Rb); regno <= (int) REGNO (Re); regno++)
    {
      reg = gen_rtx_REG (SImode, regno);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
      XVECEXP (parallel_insn, 0, par_index) = push_rtx;
      RTX_FRAME_RELATED_P (push_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }

  /* Create (set mem fp).  */
  reg = gen_rtx_REG (SImode, FP_REGNUM);
  mem = gen_frame_mem (SImode, plus_constant (Pmode,
					      stack_pointer_rtx,
					      offset));
  push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
  XVECEXP (parallel_insn, 0, par_index) = push_rtx;
  RTX_FRAME_RELATED_P (push_rtx) = 1;
  offset = offset + 4;
  par_index++;
  /* Create (set mem gp).  */
  reg = gen_rtx_REG (SImode, GP_REGNUM);
  mem = gen_frame_mem (SImode, plus_constant (Pmode,
					      stack_pointer_rtx,
					      offset));
  push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
  XVECEXP (parallel_insn, 0, par_index) = push_rtx;
  RTX_FRAME_RELATED_P (push_rtx) = 1;
  offset = offset + 4;
  par_index++;
  /* Create (set mem lp).  */
  reg = gen_rtx_REG (SImode, LP_REGNUM);
  mem = gen_frame_mem (SImode, plus_constant (Pmode,
					      stack_pointer_rtx,
					      offset));
  push_rtx = gen_rtx_SET (VOIDmode, mem, reg);
  XVECEXP (parallel_insn, 0, par_index) = push_rtx;
  RTX_FRAME_RELATED_P (push_rtx) = 1;
  offset = offset + 4;
  par_index++;

  /* Create (set sp sp-x-imm8u).  */

  /* We need to re-calculate the offset value again for adjustment.  */
  offset = -(num_use_regs * 4);
  adjust_sp_rtx
    = gen_rtx_SET (VOIDmode,
		   stack_pointer_rtx,
		   plus_constant (Pmode,
				  stack_pointer_rtx,
				  offset - INTVAL (imm8u)));
  XVECEXP (parallel_insn, 0, par_index) = adjust_sp_rtx;
  RTX_FRAME_RELATED_P (adjust_sp_rtx) = 1;

  return parallel_insn;
}

/* Function to create a parallel rtx pattern
   which presents stack v3pop behavior.
   The overall concept are:
     "pop registers from memory",
     "adjust stack pointer".  */
static rtx
nds32_gen_stack_v3pop (rtx Rb,
		       rtx Re,
		       rtx En4 ATTRIBUTE_UNUSED,
		       rtx imm8u)
{
  int regno;
  int num_use_regs;
  int par_index;
  int offset;

  rtx reg;
  rtx mem;
  rtx pop_rtx;
  rtx adjust_sp_rtx;
  rtx parallel_insn;

  /* We need to provide a customized rtx which contains
     necessary information for data analysis,
     so we create a parallel rtx like this:
     (parallel [(set (reg:SI Rb)
                     (mem (reg:SI SP_REGNUM)))
                (set (reg:SI Rb+1)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 4))))
                ...
                (set (reg:SI Re)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 16))))
                (set (reg:SI FP_REGNUM)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 20))))
                (set (reg:SI GP_REGNUM)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 24))))
                (set (reg:SI LP_REGNUM)
                     (mem (plus (reg:SI SP_REGNUM) (const_int 28))))
                (set (reg:SI SP_REGNUM)
                     (plus (reg:SI SP_REGNUM) (const_int 32+imm8u)))]) */

  /* Calculate the number of registers that will be poped.
     Since $fp, $gp, and $lp is always poped with v3pop instruction,
     we need to count these three registers.
     Under v3push, Rb is $r6, while Re is $r6, $r8, $r10, or $r14.
     So there is no need to worry about Rb=Re=SP_REGNUM case.  */
  num_use_regs = REGNO (Re) - REGNO (Rb) + 1 + 3;

  /* In addition to used registers,
     we need one more space for (set sp sp+x+imm8u) rtx.  */
  parallel_insn = gen_rtx_PARALLEL (VOIDmode,
				    rtvec_alloc (num_use_regs + 1));
  par_index = 0;

  /* Initialize offset and start to create pop behavior.  */
  offset = 0;

  /* Create (set regX mem) from Rb, Rb+1 up to Re.
     Under v3pop, Rb is $r6, while Re is $r6, $r8, $r10, or $r14.
     So there is no need to worry about Rb=Re=SP_REGNUM case.  */
  for (regno = REGNO (Rb); regno <= (int) REGNO (Re); regno++)
    {
      reg = gen_rtx_REG (SImode, regno);
      mem = gen_frame_mem (SImode, plus_constant (Pmode,
						  stack_pointer_rtx,
						  offset));
      pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
      XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
      RTX_FRAME_RELATED_P (pop_rtx) = 1;
      offset = offset + 4;
      par_index++;
    }

  /* Create (set fp mem).  */
  reg = gen_rtx_REG (SImode, FP_REGNUM);
  mem = gen_frame_mem (SImode, plus_constant (Pmode,
					      stack_pointer_rtx,
					      offset));
  pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
  XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
  RTX_FRAME_RELATED_P (pop_rtx) = 1;
  offset = offset + 4;
  par_index++;
  /* Create (set gp mem).  */
  reg = gen_rtx_REG (SImode, GP_REGNUM);
  mem = gen_frame_mem (SImode, plus_constant (Pmode,
					      stack_pointer_rtx,
					      offset));
  pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
  XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
  RTX_FRAME_RELATED_P (pop_rtx) = 1;
  offset = offset + 4;
  par_index++;
  /* Create (set lp mem ).  */
  reg = gen_rtx_REG (SImode, LP_REGNUM);
  mem = gen_frame_mem (SImode, plus_constant (Pmode,
					      stack_pointer_rtx,
					      offset));
  pop_rtx = gen_rtx_SET (VOIDmode, reg, mem);
  XVECEXP (parallel_insn, 0, par_index) = pop_rtx;
  RTX_FRAME_RELATED_P (pop_rtx) = 1;
  offset = offset + 4;
  par_index++;

  /* Create (set sp sp+x+imm8u).  */

  /* The offset value is already in place.  No need to re-calculate it.  */
  adjust_sp_rtx
    = gen_rtx_SET (VOIDmode,
		   stack_pointer_rtx,
		   plus_constant (Pmode,
				  stack_pointer_rtx,
				  offset + INTVAL (imm8u)));
  XVECEXP (parallel_insn, 0, par_index) = adjust_sp_rtx;
  RTX_FRAME_RELATED_P (adjust_sp_rtx) = 1;

  return parallel_insn;
}

/* Function that may creates more instructions
   for large value on adjusting stack pointer.

   In nds32 target, 'addi' can be used for stack pointer
   adjustment in prologue/epilogue stage.
   However, sometimes there are too many local variables so that
   the adjustment value is not able to be fit in the 'addi' instruction.
   One solution is to move value into a register
   and then use 'add' instruction.
   In practice, we use TA_REGNUM ($r15) to accomplish this purpose.
   Also, we need to return zero for sp adjustment so that
   proglogue/epilogue knows there is no need to create 'addi' instruction.  */
static int
nds32_force_addi_stack_int (int full_value)
{
  int adjust_value;

  rtx tmp_reg;
  rtx sp_adjust_insn;

  if (!satisfies_constraint_Is15 (GEN_INT (full_value)))
    {
      /* The value is not able to fit in single addi instruction.
         Create more instructions of moving value into a register
         and then add stack pointer with it.  */

      /* $r15 is going to be temporary register to hold the value.  */
      tmp_reg = gen_rtx_REG (SImode, TA_REGNUM);

      /* Create one more instruction to move value
         into the temporary register.  */
      emit_move_insn (tmp_reg, GEN_INT (full_value));

      /* Create new 'add' rtx.  */
      sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
				   stack_pointer_rtx,
				   tmp_reg);
      /* Emit rtx into insn list and receive its transformed insn rtx.  */
      sp_adjust_insn = emit_insn (sp_adjust_insn);

      /* At prologue, we need to tell GCC that this is frame related insn,
         so that we can consider this instruction to output debug information.
         If full_value is NEGATIVE, it means this function
         is invoked by expand_prologue.  */
      if (full_value < 0)
	{
	  /* Because (tmp_reg <- full_value) may be split into two
	     rtl patterns, we can not set its RTX_FRAME_RELATED_P.
	     We need to construct another (sp <- sp + full_value)
	     and then insert it into sp_adjust_insn's reg note to
	     represent a frame related expression.
	     GCC knows how to refer it and output debug information.  */

	  rtx plus_rtx;
	  rtx set_rtx;

	  plus_rtx = plus_constant (Pmode, stack_pointer_rtx, full_value);
	  set_rtx = gen_rtx_SET (VOIDmode, stack_pointer_rtx, plus_rtx);
	  add_reg_note (sp_adjust_insn, REG_FRAME_RELATED_EXPR, set_rtx);

	  RTX_FRAME_RELATED_P (sp_adjust_insn) = 1;
	}

      /* We have used alternative way to adjust stack pointer value.
         Return zero so that prologue/epilogue
         will not generate other instructions.  */
      return 0;
    }
  else
    {
      /* The value is able to fit in addi instruction.
         However, remember to make it to be positive value
         because we want to return 'adjustment' result.  */
      adjust_value = (full_value < 0) ? (-full_value) : (full_value);

      return adjust_value;
    }
}

/* Return true if MODE/TYPE need double word alignment.  */
static bool
nds32_needs_double_word_align (enum machine_mode mode, const_tree type)
{
  unsigned int align;

  /* Pick up the alignment according to the mode or type.  */
  align = NDS32_MODE_TYPE_ALIGN (mode, type);

  return (align > PARM_BOUNDARY);
}

/* Return true if FUNC is a naked function.  */
static bool
nds32_naked_function_p (tree func)
{
  tree t;

  if (TREE_CODE (func) != FUNCTION_DECL)
    abort ();

  t = lookup_attribute ("naked", DECL_ATTRIBUTES (func));

  return (t != NULL_TREE);
}

/* Function that check if 'X' is a valid address register.
   The variable 'STRICT' is very important to
   make decision for register number.

   STRICT : true
     => We are in reload pass or after reload pass.
        The register number should be strictly limited in general registers.

   STRICT : false
     => Before reload pass, we are free to use any register number.  */
static bool
nds32_address_register_rtx_p (rtx x, bool strict)
{
  int regno;

  if (GET_CODE (x) != REG)
    return false;

  regno = REGNO (x);

  if (strict)
    return REGNO_OK_FOR_BASE_P (regno);
  else
    return true;
}

/* Function that check if 'INDEX' is valid to be a index rtx for address.

   OUTER_MODE : Machine mode of outer address rtx.
        INDEX : Check if this rtx is valid to be a index for address.
       STRICT : If it is true, we are in reload pass or after reload pass.  */
static bool
nds32_legitimate_index_p (enum machine_mode outer_mode,
			  rtx index,
			  bool strict)
{
  int regno;
  rtx op0;
  rtx op1;

  switch (GET_CODE (index))
    {
    case REG:
      regno = REGNO (index);
      /* If we are in reload pass or after reload pass,
         we need to limit it to general register.  */
      if (strict)
	return REGNO_OK_FOR_INDEX_P (regno);
      else
	return true;

    case CONST_INT:
      /* The alignment of the integer value is determined by 'outer_mode'.  */
      if (GET_MODE_SIZE (outer_mode) == 1)
	{
	  /* Further check if the value is legal for the 'outer_mode'.  */
	  if (!satisfies_constraint_Is15 (index))
	    return false;

	  /* Pass all test, the value is valid, return true.  */
	  return true;
	}
      if (GET_MODE_SIZE (outer_mode) == 2
	  && NDS32_HALF_WORD_ALIGN_P (INTVAL (index)))
	{
	  /* Further check if the value is legal for the 'outer_mode'.  */
	  if (!satisfies_constraint_Is16 (index))
	    return false;

	  /* Pass all test, the value is valid, return true.  */
	  return true;
	}
      if (GET_MODE_SIZE (outer_mode) == 4
	  && NDS32_SINGLE_WORD_ALIGN_P (INTVAL (index)))
	{
	  /* Further check if the value is legal for the 'outer_mode'.  */
	  if (!satisfies_constraint_Is17 (index))
	    return false;

	  /* Pass all test, the value is valid, return true.  */
	  return true;
	}
      if (GET_MODE_SIZE (outer_mode) == 8
	  && NDS32_SINGLE_WORD_ALIGN_P (INTVAL (index)))
	{
	  /* Further check if the value is legal for the 'outer_mode'.  */
	  if (!satisfies_constraint_Is17 (gen_int_mode (INTVAL (index) + 4,
							SImode)))
	    return false;

	  /* Pass all test, the value is valid, return true.  */
	  return true;
	}

      return false;

    case MULT:
      op0 = XEXP (index, 0);
      op1 = XEXP (index, 1);

      if (REG_P (op0) && CONST_INT_P (op1))
	{
	  int multiplier;
	  multiplier = INTVAL (op1);

	  /* We only allow (mult reg const_int_1)
	     or (mult reg const_int_2) or (mult reg const_int_4).  */
	  if (multiplier != 1 && multiplier != 2 && multiplier != 4)
	    return false;

	  regno = REGNO (op0);
	  /* Limit it in general registers if we are
	     in reload pass or after reload pass.  */
	  if(strict)
	    return REGNO_OK_FOR_INDEX_P (regno);
	  else
	    return true;
	}

      return false;

    case ASHIFT:
      op0 = XEXP (index, 0);
      op1 = XEXP (index, 1);

      if (REG_P (op0) && CONST_INT_P (op1))
	{
	  int sv;
	  /* op1 is already the sv value for use to do left shift.  */
	  sv = INTVAL (op1);

	  /* We only allow (ashift reg const_int_0)
	     or (ashift reg const_int_1) or (ashift reg const_int_2).  */
	  if (sv != 0 && sv != 1 && sv !=2)
	    return false;

	  regno = REGNO (op0);
	  /* Limit it in general registers if we are
	     in reload pass or after reload pass.  */
	  if(strict)
	    return REGNO_OK_FOR_INDEX_P (regno);
	  else
	    return true;
	}

      return false;

    default:
      return false;
    }
}

/* ------------------------------------------------------------------------ */

/* PART 3: Implement target hook stuff definitions.  */

/* Register Classes.  */

static unsigned char
nds32_class_max_nregs (reg_class_t rclass ATTRIBUTE_UNUSED,
		       enum machine_mode mode)
{
  /* Return the maximum number of consecutive registers
     needed to represent "mode" in a register of "rclass".  */
  return ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD);
}

static int
nds32_register_priority (int hard_regno)
{
  /* Encourage to use r0-r7 for LRA when optimize for size.  */
  if (optimize_size && hard_regno < 8)
    return 4;
  return 3;
}


/* Stack Layout and Calling Conventions.  */

/* There are three kinds of pointer concepts using in GCC compiler:

     frame pointer: A pointer to the first location of local variables.
     stack pointer: A pointer to the top of a stack frame.
     argument pointer: A pointer to the incoming arguments.

   In nds32 target calling convention, we are using 8-byte alignment.
   Besides, we would like to have each stack frame of a function includes:

     [Block A]
       1. previous hard frame pointer
       2. return address
       3. callee-saved registers
       4. <padding bytes> (we will calculte in nds32_compute_stack_frame()
                           and save it at
                           cfun->machine->callee_saved_area_padding_bytes)

     [Block B]
       1. local variables
       2. spilling location
       3. <padding bytes> (it will be calculated by GCC itself)
       4. incoming arguments
       5. <padding bytes> (it will be calculated by GCC itself)

     [Block C]
       1. <padding bytes> (it will be calculated by GCC itself)
       2. outgoing arguments

   We 'wrap' these blocks together with
   hard frame pointer ($r28) and stack pointer ($r31).
   By applying the basic frame/stack/argument pointers concept,
   the layout of a stack frame shoule be like this:

                            |    |
       old stack pointer ->  ----
                            |    | \
                            |    |   saved arguments for
                            |    |   vararg functions
                            |    | /
      hard frame pointer ->   --
      & argument pointer    |    | \
                            |    |   previous hardware frame pointer
                            |    |   return address
                            |    |   callee-saved registers
                            |    | /
           frame pointer ->   --
                            |    | \
                            |    |   local variables
                            |    |   and incoming arguments
                            |    | /
                              --
                            |    | \
                            |    |   outgoing
                            |    |   arguments
                            |    | /
           stack pointer ->  ----

  $SFP and $AP are used to represent frame pointer and arguments pointer,
  which will be both eliminated as hard frame pointer.  */

/* -- Eliminating Frame Pointer and Arg Pointer.  */

static bool
nds32_can_eliminate (const int from_reg, const int to_reg)
{
  if (from_reg == ARG_POINTER_REGNUM && to_reg == STACK_POINTER_REGNUM)
    return true;

  if (from_reg == ARG_POINTER_REGNUM && to_reg == HARD_FRAME_POINTER_REGNUM)
    return true;

  if (from_reg == FRAME_POINTER_REGNUM && to_reg == STACK_POINTER_REGNUM)
    return true;

  if (from_reg == FRAME_POINTER_REGNUM && to_reg == HARD_FRAME_POINTER_REGNUM)
    return true;

  return false;
}

/* -- Passing Arguments in Registers.  */

static rtx
nds32_function_arg (cumulative_args_t ca, enum machine_mode mode,
		    const_tree type, bool named)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (ca);

  /* The last time this hook is called,
     it is called with MODE == VOIDmode.  */
  if (mode == VOIDmode)
    return NULL_RTX;

  /* For nameless arguments, they are passed on the stack.  */
  if (!named)
    return NULL_RTX;

  /* If there are still registers available, return it.  */
  if (NDS32_ARG_PASS_IN_REG_P (cum->reg_offset, mode, type))
    {
      /* Pick up the next available register number.  */
      unsigned int regno;

      regno = NDS32_AVAILABLE_REGNUM_FOR_ARG (cum->reg_offset, mode, type);
      return gen_rtx_REG (mode, regno);
    }
  else
    {
      /* No register available, return NULL_RTX.
         The compiler will use stack to pass argument instead.  */
      return NULL_RTX;
    }
}

static void
nds32_function_arg_advance (cumulative_args_t ca, enum machine_mode mode,
			    const_tree type, bool named)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (ca);

  /* Advance next register for use.
     Only named argument could be advanced.  */
  if (named)
    {
      cum->reg_offset
	= NDS32_AVAILABLE_REGNUM_FOR_ARG (cum->reg_offset, mode, type)
	  - NDS32_GPR_ARG_FIRST_REGNUM
	  + NDS32_NEED_N_REGS_FOR_ARG (mode, type);
    }
}

static unsigned int
nds32_function_arg_boundary (enum machine_mode mode, const_tree type)
{
  return (nds32_needs_double_word_align (mode, type)
	  ? NDS32_DOUBLE_WORD_ALIGNMENT
	  : PARM_BOUNDARY);
}

/* -- How Scalar Function Values Are Returned.  */

static rtx
nds32_function_value (const_tree ret_type,
		      const_tree fn_decl_or_type ATTRIBUTE_UNUSED,
		      bool outgoing ATTRIBUTE_UNUSED)
{
  enum machine_mode mode;
  int unsignedp;

  mode = TYPE_MODE (ret_type);
  unsignedp = TYPE_UNSIGNED (ret_type);

  mode = promote_mode (ret_type, mode, &unsignedp);

  return gen_rtx_REG (mode, NDS32_GPR_RET_FIRST_REGNUM);
}

static rtx
nds32_libcall_value (enum machine_mode mode,
		     const_rtx fun ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (mode, NDS32_GPR_RET_FIRST_REGNUM);
}

static bool
nds32_function_value_regno_p (const unsigned int regno)
{
  return (regno == NDS32_GPR_RET_FIRST_REGNUM);
}

/* -- Function Entry and Exit.  */

/* The content produced from this function
   will be placed before prologue body.  */
static void
nds32_asm_function_prologue (FILE *file,
			     HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  int r;
  const char *func_name;
  tree attrs;
  tree name;

  /* All stack frame information is supposed to be
     already computed when expanding prologue.
     The result is in cfun->machine.
     DO NOT call nds32_compute_stack_frame() here
     because it may corrupt the essential information.  */

  fprintf (file, "\t! BEGIN PROLOGUE\n");
  fprintf (file, "\t!     fp needed: %d\n", frame_pointer_needed);
  fprintf (file, "\t!  pretend_args: %d\n", cfun->machine->va_args_size);
  fprintf (file, "\t!    local_size: %d\n", cfun->machine->local_size);
  fprintf (file, "\t! out_args_size: %d\n", cfun->machine->out_args_size);

  /* Use df_regs_ever_live_p() to detect if the register
     is ever used in the current function.  */
  fprintf (file, "\t! registers ever_live: ");
  for (r = 0; r < 32; r++)
    {
      if (df_regs_ever_live_p (r))
	fprintf (file, "%s, ", reg_names[r]);
    }
  fputc ('\n', file);

  /* Display the attributes of this function.  */
  fprintf (file, "\t! function attributes: ");
  /* Get the attributes tree list.
     Note that GCC builds attributes list with reverse order.  */
  attrs = DECL_ATTRIBUTES (current_function_decl);

  /* If there is no any attribute, print out "None".  */
  if (!attrs)
    fprintf (file, "None");

  /* If there are some attributes, try if we need to
     construct isr vector information.  */
  func_name = IDENTIFIER_POINTER (DECL_NAME (current_function_decl));
  nds32_construct_isr_vectors_information (attrs, func_name);

  /* Display all attributes of this function.  */
  while (attrs)
    {
      name = TREE_PURPOSE (attrs);
      fprintf (file, "%s ", IDENTIFIER_POINTER (name));

      /* Pick up the next attribute.  */
      attrs = TREE_CHAIN (attrs);
    }
  fputc ('\n', file);
}

/* After rtl prologue has been expanded, this function is used.  */
static void
nds32_asm_function_end_prologue (FILE *file)
{
  fprintf (file, "\t! END PROLOGUE\n");

  /* If frame pointer is NOT needed and -mfp-as-gp is issued,
     we can generate special directive: ".omit_fp_begin"
     to guide linker doing fp-as-gp optimization.
     However, for a naked function, which means
     it should not have prologue/epilogue,
     using fp-as-gp still requires saving $fp by push/pop behavior and
     there is no benefit to use fp-as-gp on such small function.
     So we need to make sure this function is NOT naked as well.  */
  if (!frame_pointer_needed
      && !cfun->machine->naked_p
      && cfun->machine->fp_as_gp_p)
    {
      fprintf (file, "\t! ----------------------------------------\n");
      fprintf (file, "\t! Guide linker to do "
		     "link time optimization: fp-as-gp\n");
      fprintf (file, "\t! We add one more instruction to "
		     "initialize $fp near to $gp location.\n");
      fprintf (file, "\t! If linker fails to use fp-as-gp transformation,\n");
      fprintf (file, "\t! this extra instruction should be "
		     "eliminated at link stage.\n");
      fprintf (file, "\t.omit_fp_begin\n");
      fprintf (file, "\tla\t$fp,_FP_BASE_\n");
      fprintf (file, "\t! ----------------------------------------\n");
    }
}

/* Before rtl epilogue has been expanded, this function is used.  */
static void
nds32_asm_function_begin_epilogue (FILE *file)
{
  /* If frame pointer is NOT needed and -mfp-as-gp is issued,
     we can generate special directive: ".omit_fp_end"
     to claim fp-as-gp optimization range.
     However, for a naked function,
     which means it should not have prologue/epilogue,
     using fp-as-gp still requires saving $fp by push/pop behavior and
     there is no benefit to use fp-as-gp on such small function.
     So we need to make sure this function is NOT naked as well.  */
  if (!frame_pointer_needed
      && !cfun->machine->naked_p
      && cfun->machine->fp_as_gp_p)
    {
      fprintf (file, "\t! ----------------------------------------\n");
      fprintf (file, "\t! Claim the range of fp-as-gp "
		     "link time optimization\n");
      fprintf (file, "\t.omit_fp_end\n");
      fprintf (file, "\t! ----------------------------------------\n");
    }

  fprintf (file, "\t! BEGIN EPILOGUE\n");
}

/* The content produced from this function
   will be placed after epilogue body.  */
static void
nds32_asm_function_epilogue (FILE *file,
			     HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  fprintf (file, "\t! END EPILOGUE\n");
}

static void
nds32_asm_output_mi_thunk (FILE *file, tree thunk ATTRIBUTE_UNUSED,
			   HOST_WIDE_INT delta,
			   HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
			   tree function)
{
  int this_regno;

  /* Make sure unwind info is emitted for the thunk if needed.  */
  final_start_function (emit_barrier (), file, 1);

  this_regno = (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function)
		? 1
		: 0);

  if (delta != 0)
    {
      if (satisfies_constraint_Is15 (GEN_INT (delta)))
	{
	  fprintf (file, "\taddi\t$r%d, $r%d, %ld\n",
		   this_regno, this_regno, delta);
	}
      else if (satisfies_constraint_Is20 (GEN_INT (delta)))
	{
	  fprintf (file, "\tmovi\t$ta, %ld\n", delta);
	  fprintf (file, "\tadd\t$r%d, $r%d, $ta\n", this_regno, this_regno);
	}
      else
	{
	  fprintf (file, "\tsethi\t$ta, hi20(%ld)\n", delta);
	  fprintf (file, "\tori\t$ta, $ta, lo12(%ld)\n", delta);
	  fprintf (file, "\tadd\t$r%d, $r%d, $ta\n", this_regno, this_regno);
	}
    }

  fprintf (file, "\tb\t");
  assemble_name (file, XSTR (XEXP (DECL_RTL (function), 0), 0));
  fprintf (file, "\n");

  final_end_function ();
}

/* -- Permitting tail calls.  */

/* Determine whether we need to enable warning for function return check.  */
static bool
nds32_warn_func_return (tree decl)
{
/* Naked functions are implemented entirely in assembly, including the
   return sequence, so suppress warnings about this.  */
  return !nds32_naked_function_p (decl);
}


/* Implementing the Varargs Macros.  */

static bool
nds32_strict_argument_naming (cumulative_args_t ca ATTRIBUTE_UNUSED)
{
  /* Return true so that all the named arguments for FUNCTION_ARG have named=1.
     If return false, for the variadic function, all named arguments EXCEPT
     the last are treated as named.  */
  return true;
}


/* Trampolines for Nested Functions.  */

static void
nds32_asm_trampoline_template (FILE *f)
{
  if (TARGET_REDUCED_REGS)
    {
      /* Trampoline is not supported on reduced-set registers yet.  */
      sorry ("a nested function is not supported for reduced registers");
    }
  else
    {
      asm_fprintf (f, "\t! Trampoline code template\n");
      asm_fprintf (f, "\t! This code fragment will be copied "
		      "into stack on demand\n");

      asm_fprintf (f, "\tmfusr\t$r16,$pc\n");
      asm_fprintf (f, "\tlwi\t$r15,[$r16 + 20] "
		      "! load nested function address\n");
      asm_fprintf (f, "\tlwi\t$r16,[$r16 + 16] "
		      "! load chain_value\n");
      asm_fprintf (f, "\tjr\t$r15\n");
    }

  /* Preserve space ($pc + 16) for saving chain_value,
     nds32_trampoline_init will fill the value in this slot.  */
  asm_fprintf (f, "\t! space for saving chain_value\n");
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);

  /* Preserve space ($pc + 20) for saving nested function address,
     nds32_trampoline_init will fill the value in this slot.  */
  asm_fprintf (f, "\t! space for saving nested function address\n");
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);
}

/* Emit RTL insns to initialize the variable parts of a trampoline.  */
static void
nds32_trampoline_init (rtx m_tramp, tree fndecl, rtx chain_value)
{
  int i;

  /* Nested function address.  */
  rtx fnaddr;
  /* The memory rtx that is going to
     be filled with chain_value.  */
  rtx chain_value_mem;
  /* The memory rtx that is going to
     be filled with nested function address.  */
  rtx nested_func_mem;

  /* Start address of trampoline code in stack, for doing cache sync.  */
  rtx sync_cache_addr;
  /* Temporary register for sync instruction.  */
  rtx tmp_reg;
  /* Instruction-cache sync instruction,
     requesting an argument as starting address.  */
  rtx isync_insn;
  /* For convenience reason of doing comparison.  */
  int tramp_align_in_bytes;

  /* Trampoline is not supported on reduced-set registers yet.  */
  if (TARGET_REDUCED_REGS)
    sorry ("a nested function is not supported for reduced registers");

  /* STEP 1: Copy trampoline code template into stack,
             fill up essential data into stack.  */

  /* Extract nested function address rtx.  */
  fnaddr = XEXP (DECL_RTL (fndecl), 0);

  /* m_tramp is memory rtx that is going to be filled with trampoline code.
     We have nds32_asm_trampoline_template() to emit template pattern.  */
  emit_block_move (m_tramp, assemble_trampoline_template (),
		   GEN_INT (TRAMPOLINE_SIZE), BLOCK_OP_NORMAL);

  /* After copying trampoline code into stack,
     fill chain_value into stack.  */
  chain_value_mem = adjust_address (m_tramp, SImode, 16);
  emit_move_insn (chain_value_mem, chain_value);
  /* After copying trampoline code int stack,
     fill nested function address into stack.  */
  nested_func_mem = adjust_address (m_tramp, SImode, 20);
  emit_move_insn (nested_func_mem, fnaddr);

  /* STEP 2: Sync instruction-cache.  */

  /* We have successfully filled trampoline code into stack.
     However, in order to execute code in stack correctly,
     we must sync instruction cache.  */
  sync_cache_addr = XEXP (m_tramp, 0);
  tmp_reg         = gen_reg_rtx (SImode);
  isync_insn      = gen_unspec_volatile_isync (tmp_reg);

  /* Because nds32_cache_block_size is in bytes,
     we get trampoline alignment in bytes for convenient comparison.  */
  tramp_align_in_bytes = TRAMPOLINE_ALIGNMENT / BITS_PER_UNIT;

  if (tramp_align_in_bytes >= nds32_cache_block_size
      && (tramp_align_in_bytes % nds32_cache_block_size) == 0)
    {
      /* Under this condition, the starting address of trampoline
         must be aligned to the starting address of each cache block
         and we do not have to worry about cross-boundary issue.  */
      for (i = 0;
	   i < (TRAMPOLINE_SIZE + nds32_cache_block_size - 1)
	       / nds32_cache_block_size;
	   i++)
	{
	  emit_move_insn (tmp_reg,
			  plus_constant (Pmode, sync_cache_addr,
					 nds32_cache_block_size * i));
	  emit_insn (isync_insn);
	}
    }
  else if (TRAMPOLINE_SIZE > nds32_cache_block_size)
    {
      /* The starting address of trampoline code
         may not be aligned to the cache block,
         so the trampoline code may be across two cache block.
         We need to sync the last element, which is 4-byte size,
         of trampoline template.  */
      for (i = 0;
	   i < (TRAMPOLINE_SIZE + nds32_cache_block_size - 1)
	       / nds32_cache_block_size;
	   i++)
	{
	  emit_move_insn (tmp_reg,
			  plus_constant (Pmode, sync_cache_addr,
					 nds32_cache_block_size * i));
	  emit_insn (isync_insn);
	}

      /* The last element of trampoline template is 4-byte size.  */
      emit_move_insn (tmp_reg,
		      plus_constant (Pmode, sync_cache_addr,
				     TRAMPOLINE_SIZE - 4));
      emit_insn (isync_insn);
    }
  else
    {
      /* This is the simplest case.
         Because TRAMPOLINE_SIZE is less than or
         equal to nds32_cache_block_size,
         we can just sync start address and
         the last element of trampoline code.  */

      /* Sync starting address of tampoline code.  */
      emit_move_insn (tmp_reg, sync_cache_addr);
      emit_insn (isync_insn);
      /* Sync the last element, which is 4-byte size,
         of trampoline template.  */
      emit_move_insn (tmp_reg,
		      plus_constant (Pmode, sync_cache_addr,
				     TRAMPOLINE_SIZE - 4));
      emit_insn (isync_insn);
    }

  /* Set instruction serialization barrier
     to guarantee the correct operations.  */
  emit_insn (gen_unspec_volatile_isb ());
}


/* Addressing Modes.  */

static bool
nds32_legitimate_address_p (enum machine_mode mode, rtx x, bool strict)
{
  /* For (mem:DI addr) or (mem:DF addr) case,
     we only allow 'addr' to be [reg], [symbol_ref],
                                [const], or [reg + const_int] pattern.  */
  if (mode == DImode || mode == DFmode)
    {
      /* Allow [Reg + const_int] addressing mode.  */
      if (GET_CODE (x) == PLUS)
	{
	  if (nds32_address_register_rtx_p (XEXP (x, 0), strict)
	      && nds32_legitimate_index_p (mode, XEXP (x, 1), strict)
	      && CONST_INT_P (XEXP (x, 1)))
	    return true;

	  else if (nds32_address_register_rtx_p (XEXP (x, 1), strict)
		   && nds32_legitimate_index_p (mode, XEXP (x, 0), strict)
		   && CONST_INT_P (XEXP (x, 0)))
	    return true;
	}

      /* Now check [reg], [symbol_ref], and [const].  */
      if (GET_CODE (x) != REG
	  && GET_CODE (x) != SYMBOL_REF
	  && GET_CODE (x) != CONST)
	return false;
    }

  /* Check if 'x' is a valid address.  */
  switch (GET_CODE (x))
    {
    case REG:
      /* (mem (reg A)) => [Ra] */
      return nds32_address_register_rtx_p (x, strict);

    case SYMBOL_REF:

      if (!TARGET_GP_DIRECT
	  && (reload_completed
	      || reload_in_progress
	      || lra_in_progress))
	return false;

      /* (mem (symbol_ref A)) => [symbol_ref] */
      return !currently_expanding_to_rtl;

    case CONST:

      if (!TARGET_GP_DIRECT
	  && (reload_completed
	      || reload_in_progress
	      || lra_in_progress))
	return false;

      /* (mem (const (...)))
         => [ + const_addr ], where const_addr = symbol_ref + const_int */
      if (GET_CODE (XEXP (x, 0)) == PLUS)
	{
	  rtx plus_op = XEXP (x, 0);

	  rtx op0 = XEXP (plus_op, 0);
	  rtx op1 = XEXP (plus_op, 1);

	  if (GET_CODE (op0) == SYMBOL_REF && CONST_INT_P (op1))
	    return true;
	  else
	    return false;
	}

	return false;

    case POST_MODIFY:
      /* (mem (post_modify (reg) (plus (reg) (reg))))
         => [Ra], Rb */
      /* (mem (post_modify (reg) (plus (reg) (const_int))))
         => [Ra], const_int */
      if (GET_CODE (XEXP (x, 0)) == REG
	  && GET_CODE (XEXP (x, 1)) == PLUS)
	{
	  rtx plus_op = XEXP (x, 1);

	  rtx op0 = XEXP (plus_op, 0);
	  rtx op1 = XEXP (plus_op, 1);

	  if (nds32_address_register_rtx_p (op0, strict)
	      && nds32_legitimate_index_p (mode, op1, strict))
	    return true;
	  else
	    return false;
	}

	return false;

    case POST_INC:
    case POST_DEC:
      /* (mem (post_inc reg)) => [Ra], 1/2/4 */
      /* (mem (post_dec reg)) => [Ra], -1/-2/-4 */
      /* The 1/2/4 or -1/-2/-4 have been displayed in nds32.md.
         We only need to deal with register Ra.  */
      if (nds32_address_register_rtx_p (XEXP (x, 0), strict))
	return true;
      else
	return false;

    case PLUS:
      /* (mem (plus reg const_int))
         => [Ra + imm] */
      /* (mem (plus reg reg))
         => [Ra + Rb] */
      /* (mem (plus (mult reg const_int) reg))
         => [Ra + Rb << sv] */
      if (nds32_address_register_rtx_p (XEXP (x, 0), strict)
	  && nds32_legitimate_index_p (mode, XEXP (x, 1), strict))
	return true;
      else if (nds32_address_register_rtx_p (XEXP (x, 1), strict)
	       && nds32_legitimate_index_p (mode, XEXP (x, 0), strict))
	return true;
      else
	return false;

    case LO_SUM:
      if (!TARGET_GP_DIRECT)
	return true;

    default:
      return false;
    }
}


/* Describing Relative Costs of Operations.  */

static int
nds32_register_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
			  reg_class_t from,
			  reg_class_t to)
{
  if (from == HIGH_REGS || to == HIGH_REGS)
    return 6;

  return 2;
}

static int
nds32_memory_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
			reg_class_t rclass ATTRIBUTE_UNUSED,
			bool in ATTRIBUTE_UNUSED)
{
  return 8;
}

/* This target hook describes the relative costs of RTL expressions.
   Return 'true' when all subexpressions of x have been processed.
   Return 'false' to sum the costs of sub-rtx, plus cost of this operation.
   Refer to gcc/rtlanal.c for more information.  */
static bool
nds32_rtx_costs (rtx x,
		 int code,
		 int outer_code,
		 int opno,
		 int *total,
		 bool speed)
{
  return nds32_rtx_costs_impl (x, code, outer_code, opno, total, speed);
}

static int
nds32_address_cost (rtx address,
		    enum machine_mode mode,
		    addr_space_t as,
		    bool speed)
{
  return nds32_address_cost_impl (address, mode, as, speed);
}


/* Defining the Output Assembler Language.  */

/* -- The Overall Framework of an Assembler File.  */

static void
nds32_asm_file_start (void)
{
  default_file_start ();

  /* Tell assembler which ABI we are using.  */
  fprintf (asm_out_file, "\t! ABI version\n");
  fprintf (asm_out_file, "\t.abi_2\n");

  /* Tell assembler that this asm code is generated by compiler.  */
  fprintf (asm_out_file, "\t! This asm file is generated by compiler\n");
  fprintf (asm_out_file, "\t.flag\tverbatim\n");
  /* Give assembler the size of each vector for interrupt handler.  */
  fprintf (asm_out_file, "\t! This vector size directive is required "
			 "for checking inconsistency on interrupt handler\n");
  fprintf (asm_out_file, "\t.vec_size\t%d\n", nds32_isr_vector_size);

  /* If user enables '-mforce-fp-as-gp' or compiles programs with -Os,
     the compiler may produce 'la $fp,_FP_BASE_' instruction
     at prologue for fp-as-gp optimization.
     We should emit weak reference of _FP_BASE_ to avoid undefined reference
     in case user does not pass '--relax' option to linker.  */
  if (TARGET_FORCE_FP_AS_GP || optimize_size)
    {
      fprintf (asm_out_file, "\t! This weak reference is required to do "
			     "fp-as-gp link time optimization\n");
      fprintf (asm_out_file, "\t.weak\t_FP_BASE_\n");
    }
  /* If user enables '-mex9', we should emit relaxation directive
     to tell linker that this file is allowed to do ex9 optimization.  */
  if (TARGET_EX9)
    {
      fprintf (asm_out_file, "\t! This relaxation directive is required "
			     "to do ex9 link time optimization\n");
      fprintf (asm_out_file, "\t.relax\tex9\n");
    }

  fprintf (asm_out_file, "\t! ------------------------------------\n");

  if (TARGET_ISA_V2)
    fprintf (asm_out_file, "\t! ISA family\t\t: %s\n", "V2");
  if (TARGET_ISA_V3)
    fprintf (asm_out_file, "\t! ISA family\t\t: %s\n", "V3");
  if (TARGET_ISA_V3M)
    fprintf (asm_out_file, "\t! ISA family\t\t: %s\n", "V3M");

  fprintf (asm_out_file, "\t! Endian setting\t: %s\n",
			 ((TARGET_BIG_ENDIAN) ? "big-endian"
					      : "little-endian"));

  fprintf (asm_out_file, "\t! ------------------------------------\n");

  fprintf (asm_out_file, "\t! Use conditional move\t\t: %s\n",
			 ((TARGET_CMOV) ? "Yes"
					: "No"));
  fprintf (asm_out_file, "\t! Use performance extension\t: %s\n",
			 ((TARGET_PERF_EXT) ? "Yes"
					    : "No"));

  fprintf (asm_out_file, "\t! ------------------------------------\n");

  fprintf (asm_out_file, "\t! V3PUSH instructions\t: %s\n",
			 ((TARGET_V3PUSH) ? "Yes"
					  : "No"));
  fprintf (asm_out_file, "\t! 16-bit instructions\t: %s\n",
			 ((TARGET_16_BIT) ? "Yes"
					  : "No"));
  fprintf (asm_out_file, "\t! GP base access\t: %s\n",
			 ((TARGET_GP_DIRECT) ? "Yes"
					     : "No"));
  fprintf (asm_out_file, "\t! Reduced registers set\t: %s\n",
			 ((TARGET_REDUCED_REGS) ? "Yes"
						: "No"));

  fprintf (asm_out_file, "\t! ------------------------------------\n");

  if (optimize_size)
    fprintf (asm_out_file, "\t! Optimization level\t: -Os\n");
  else
    fprintf (asm_out_file, "\t! Optimization level\t: -O%d\n", optimize);

  fprintf (asm_out_file, "\t! ------------------------------------\n");

  fprintf (asm_out_file, "\t! Cache block size\t: %d\n",
			 nds32_cache_block_size);

  fprintf (asm_out_file, "\t! ------------------------------------\n");

  nds32_asm_file_start_for_isr ();
}

static void
nds32_asm_file_end (void)
{
  nds32_asm_file_end_for_isr ();

  fprintf (asm_out_file, "\t! ------------------------------------\n");
}

/* -- Output and Generation of Labels.  */

static void
nds32_asm_globalize_label (FILE *stream, const char *name)
{
  fputs ("\t.global\t", stream);
  assemble_name (stream, name);
  fputs ("\n", stream);
}

/* -- Output of Assembler Instructions.  */

static void
nds32_print_operand (FILE *stream, rtx x, int code)
{
  int op_value;

  switch (code)
    {
    case 0 :
      /* Do nothing special.  */
      break;

    case 'V':
      /* 'x' is supposed to be CONST_INT, get the value.  */
      gcc_assert (CONST_INT_P (x));
      op_value = INTVAL (x);

      /* According to the Andes architecture,
         the system/user register index range is 0 ~ 1023.
         In order to avoid conflict between user-specified-integer value
         and enum-specified-register value,
         the 'enum nds32_intrinsic_registers' value
         in nds32_intrinsic.h starts from 1024.  */
      if (op_value < 1024 && op_value >= 0)
	{
	  /* If user gives integer value directly (0~1023),
	     we just print out the value.  */
	  fprintf (stream, "%d", op_value);
	}
      else if (op_value < 0
	       || op_value >= ((int) ARRAY_SIZE (nds32_intrinsic_register_names)
			       + 1024))
	{
	  /* The enum index value for array size is out of range.  */
	  error ("intrinsic register index is out of range");
	}
      else
	{
	  /* If user applies normal way with __NDS32_REG_XXX__ enum data,
	     we can print out register name.  Remember to substract 1024.  */
	  fprintf (stream, "%s",
			   nds32_intrinsic_register_names[op_value - 1024]);
	}

      /* No need to handle following process, so return immediately.  */
      return;

    default :
      /* Unknown flag.  */
      output_operand_lossage ("invalid operand output code");
      break;
    }

  switch (GET_CODE (x))
    {
    case LABEL_REF:
    case SYMBOL_REF:
      output_addr_const (stream, x);
      break;

    case REG:
      /* Forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REGNO (x) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");

      /* Normal cases, print out register name.  */
      fputs (reg_names[REGNO (x)], stream);
      break;

    case MEM:
      output_address (XEXP (x, 0));
      break;

    case CODE_LABEL:
    case CONST_INT:
    case CONST:
      output_addr_const (stream, x);
      break;

    default:
      /* Generally, output_addr_const () is able to handle most cases.
         We want to see what CODE could appear,
         so we use gcc_unreachable() to stop it.  */
      debug_rtx (x);
      gcc_unreachable ();
      break;
    }
}

static void
nds32_print_operand_address (FILE *stream, rtx x)
{
  rtx op0, op1;

  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
    case CONST:
      /* [ + symbol_ref] */
      /* [ + const_addr], where const_addr = symbol_ref + const_int */
      fputs ("[ + ", stream);
      output_addr_const (stream, x);
      fputs ("]", stream);
      break;

    case REG:
      /* Forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REGNO (x) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");

      /* [Ra] */
      fprintf (stream, "[%s]", reg_names[REGNO (x)]);
      break;

    case PLUS:
      op0 = XEXP (x, 0);
      op1 = XEXP (x, 1);

      /* Checking op0, forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REG_P (op0)
	  && REGNO (op0) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");
      /* Checking op1, forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REG_P (op1)
	  && REGNO (op1) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");

      if (REG_P (op0) && CONST_INT_P (op1))
	{
	  /* [Ra + imm] */
	  fprintf (stream, "[%s + (%d)]",
			   reg_names[REGNO (op0)], (int)INTVAL (op1));
	}
      else if (REG_P (op0) && REG_P (op1))
	{
	  /* [Ra + Rb] */
	  fprintf (stream, "[%s + %s]",
			   reg_names[REGNO (op0)], reg_names[REGNO (op1)]);
	}
      else if (GET_CODE (op0) == MULT && REG_P (op1))
	{
	  /* [Ra + Rb << sv]
	     From observation, the pattern looks like:
	     (plus:SI (mult:SI (reg:SI 58)
	                       (const_int 4 [0x4]))
	              (reg/f:SI 57)) */
	  int sv;

	  /* We need to set sv to output shift value.  */
	  if (INTVAL (XEXP (op0, 1)) == 1)
	    sv = 0;
	  else if (INTVAL (XEXP (op0, 1)) == 2)
	    sv = 1;
	  else if (INTVAL (XEXP (op0, 1)) == 4)
	    sv = 2;
	  else
	    gcc_unreachable ();

	  fprintf (stream, "[%s + %s << %d]",
			   reg_names[REGNO (op1)],
			   reg_names[REGNO (XEXP (op0, 0))],
			   sv);
	}
      else
	{
	  /* The control flow is not supposed to be here.  */
	  debug_rtx (x);
	  gcc_unreachable ();
	}

      break;

    case POST_MODIFY:
      /* (post_modify (regA) (plus (regA) (regB)))
         (post_modify (regA) (plus (regA) (const_int)))
         We would like to extract
         regA and regB (or const_int) from plus rtx.  */
      op0 = XEXP (XEXP (x, 1), 0);
      op1 = XEXP (XEXP (x, 1), 1);

      /* Checking op0, forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REG_P (op0)
	  && REGNO (op0) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");
      /* Checking op1, forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REG_P (op1)
	  && REGNO (op1) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");

      if (REG_P (op0) && REG_P (op1))
	{
	  /* [Ra], Rb */
	  fprintf (stream, "[%s], %s",
			   reg_names[REGNO (op0)], reg_names[REGNO (op1)]);
	}
      else if (REG_P (op0) && CONST_INT_P (op1))
	{
	  /* [Ra], imm */
	  fprintf (stream, "[%s], %d",
			   reg_names[REGNO (op0)], (int)INTVAL (op1));
	}
      else
	{
	  /* The control flow is not supposed to be here.  */
	  debug_rtx (x);
	  gcc_unreachable ();
	}

      break;

    case POST_INC:
    case POST_DEC:
      op0 = XEXP (x, 0);

      /* Checking op0, forbid using static chain register ($r16)
         on reduced-set registers configuration.  */
      if (TARGET_REDUCED_REGS
	  && REG_P (op0)
	  && REGNO (op0) == STATIC_CHAIN_REGNUM)
	sorry ("a nested function is not supported for reduced registers");

      if (REG_P (op0))
	{
	  /* "[Ra], 1/2/4" or "[Ra], -1/-2/-4"
	     The 1/2/4 or -1/-2/-4 have been displayed in nds32.md.
	     We only need to deal with register Ra.  */
	  fprintf (stream, "[%s]", reg_names[REGNO (op0)]);
	}
      else
	{
	  /* The control flow is not supposed to be here.  */
	  debug_rtx (x);
	  gcc_unreachable ();
	}

      break;

    default :
      /* Generally, output_addr_const () is able to handle most cases.
         We want to see what CODE could appear,
         so we use gcc_unreachable() to stop it.  */
      debug_rtx (x);
      gcc_unreachable ();
      break;
    }
}


/* Defining target-specific uses of __attribute__.  */

/* Add some checking after merging attributes.  */
static tree
nds32_merge_decl_attributes (tree olddecl, tree newdecl)
{
  tree combined_attrs;

  /* Create combined attributes.  */
  combined_attrs = merge_attributes (DECL_ATTRIBUTES (olddecl),
				     DECL_ATTRIBUTES (newdecl));

  /* Since newdecl is acutally a duplicate of olddecl,
     we can take olddecl for some operations.  */
  if (TREE_CODE (olddecl) == FUNCTION_DECL)
    {
      /* Check isr-specific attributes conflict.  */
      nds32_check_isr_attrs_conflict (olddecl, combined_attrs);
    }

  return combined_attrs;
}

/* Add some checking when inserting attributes.  */
static void
nds32_insert_attributes (tree decl, tree *attributes)
{
  /* For function declaration, we need to check isr-specific attributes:
       1. Call nds32_check_isr_attrs_conflict() to check any conflict.
       2. Check valid integer value for interrupt/exception.
       3. Check valid integer value for reset.
       4. Check valid function for nmi/warm.  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      tree func_attrs;
      tree intr, excp, reset;

      /* Pick up function attributes.  */
      func_attrs = *attributes;

      /* 1. Call nds32_check_isr_attrs_conflict() to check any conflict.  */
      nds32_check_isr_attrs_conflict (decl, func_attrs);

      /* Now we are starting to check valid id value
         for interrupt/exception/reset.
         Note that we ONLY check its validity here.
         To construct isr vector information, it is still performed
         by nds32_construct_isr_vectors_information().  */
      intr  = lookup_attribute ("interrupt", func_attrs);
      excp  = lookup_attribute ("exception", func_attrs);
      reset = lookup_attribute ("reset", func_attrs);

      if (intr || excp)
	{
	  /* Deal with interrupt/exception.  */
	  tree id_list;
	  unsigned int lower_bound, upper_bound;

	  /* The way to handle interrupt or exception is the same,
	     we just need to take care of actual vector number.
	     For interrupt(0..63), the actual vector number is (9..72).
	     For exception(1..8), the actual vector number is (1..8).  */
	  lower_bound = (intr) ? (0) : (1);
	  upper_bound = (intr) ? (63) : (8);

	  /* Prepare id list so that we can traverse id value.  */
	  id_list = (intr) ? (TREE_VALUE (intr)) : (TREE_VALUE (excp));

	  /* 2. Check valid integer value for interrupt/exception.  */
	  while (id_list)
	    {
	      tree id;

	      /* Pick up each vector id value.  */
	      id = TREE_VALUE (id_list);
	      /* Issue error if it is not a valid integer value.  */
	      if (TREE_CODE (id) != INTEGER_CST
		  || wi::ltu_p (id, lower_bound)
		  || wi::gtu_p (id, upper_bound))
		error ("invalid id value for interrupt/exception attribute");

	      /* Advance to next id.  */
	      id_list = TREE_CHAIN (id_list);
	    }
	}
      else if (reset)
	{
	  /* Deal with reset.  */
	  tree id_list;
	  tree id;
	  tree nmi, warm;
	  unsigned int lower_bound;
	  unsigned int upper_bound;

	  /* Prepare id_list and identify id value so that
	     we can check if total number of vectors is valid.  */
	  id_list = TREE_VALUE (reset);
	  id = TREE_VALUE (id_list);

	  /* The maximum numbers for user's interrupt is 64.  */
	  lower_bound = 0;
	  upper_bound = 64;

	  /* 3. Check valid integer value for reset.  */
	  if (TREE_CODE (id) != INTEGER_CST
	      || wi::ltu_p (id, lower_bound)
	      || wi::gtu_p (id, upper_bound))
	    error ("invalid id value for reset attribute");

	  /* 4. Check valid function for nmi/warm.  */
	  nmi  = lookup_attribute ("nmi", func_attrs);
	  warm = lookup_attribute ("warm", func_attrs);

	  if (nmi != NULL_TREE)
	    {
	      tree nmi_func_list;
	      tree nmi_func;

	      nmi_func_list = TREE_VALUE (nmi);
	      nmi_func = TREE_VALUE (nmi_func_list);

	      /* Issue error if it is not a valid nmi function.  */
	      if (TREE_CODE (nmi_func) != IDENTIFIER_NODE)
		error ("invalid nmi function for reset attribute");
	    }

	  if (warm != NULL_TREE)
	    {
	      tree warm_func_list;
	      tree warm_func;

	      warm_func_list = TREE_VALUE (warm);
	      warm_func = TREE_VALUE (warm_func_list);

	      /* Issue error if it is not a valid warm function.  */
	      if (TREE_CODE (warm_func) != IDENTIFIER_NODE)
		error ("invalid warm function for reset attribute");
	    }
	}
      else
	{
	  /* No interrupt, exception, or reset attribute is set.  */
	  return;
	}
    }
}

static bool
nds32_option_pragma_parse (tree args ATTRIBUTE_UNUSED,
			   tree pop_target ATTRIBUTE_UNUSED)
{
  /* Currently, we do not parse any pragma target by ourself,
     so just simply return false.  */
  return false;
}

static void
nds32_option_override (void)
{
  /* After all the command options have been parsed,
     we shall deal with some flags for changing compiler settings.  */

  /* At first, we check if we have to strictly
     set some flags based on ISA family.  */
  if (TARGET_ISA_V2)
    {
      /* Under V2 ISA, we need to strictly disable TARGET_V3PUSH.  */
      target_flags &= ~MASK_V3PUSH;
    }
  if (TARGET_ISA_V3)
    {
      /* Under V3 ISA, currently nothing should be strictly set.  */
    }
  if (TARGET_ISA_V3M)
    {
      /* Under V3M ISA, we need to strictly enable TARGET_REDUCED_REGS.  */
      target_flags |= MASK_REDUCED_REGS;
      /* Under V3M ISA, we need to strictly disable TARGET_PERF_EXT.  */
      target_flags &= ~MASK_PERF_EXT;
    }

  /* See if we are using reduced-set registers:
       $r0~$r5, $r6~$r10, $r15, $r28, $r29, $r30, $r31
     If so, we must forbid using $r11~$r14, $r16~$r27.  */
  if (TARGET_REDUCED_REGS)
    {
      int r;

      /* Prevent register allocator from
         choosing it as doing register allocation.  */
      for (r = 11; r <= 14; r++)
	fixed_regs[r] = call_used_regs[r] = 1;
      for (r = 16; r <= 27; r++)
	fixed_regs[r] = call_used_regs[r] = 1;
    }

  /* See if user explicitly would like to use fp-as-gp optimization.
     If so, we must prevent $fp from being allocated
     during register allocation.  */
  if (TARGET_FORCE_FP_AS_GP)
    fixed_regs[FP_REGNUM] = call_used_regs[FP_REGNUM] = 1;

  if (!TARGET_16_BIT)
    {
      /* Under no 16 bit ISA, we need to strictly disable TARGET_V3PUSH.  */
      target_flags &= ~MASK_V3PUSH;
    }

  /* Currently, we don't support PIC code generation yet.  */
  if (flag_pic)
    sorry ("not support -fpic");
}


/* Miscellaneous Parameters.  */

static void
nds32_init_builtins (void)
{
  nds32_init_builtins_impl ();
}

static rtx
nds32_expand_builtin (tree exp,
		      rtx target,
		      rtx subtarget,
		      enum machine_mode mode,
		      int ignore)
{
  return nds32_expand_builtin_impl (exp, target, subtarget, mode, ignore);
}


/* ------------------------------------------------------------------------ */

/* PART 4: Implemet extern function definitions,
           the prototype is in nds32-protos.h.  */

/* Defining Data Structures for Per-function Information.  */

void
nds32_init_expanders (void)
{
  /* Arrange to initialize and mark the machine per-function status.  */
  init_machine_status = nds32_init_machine_status;
}


/* Register Usage.  */

/* -- How Values Fit in Registers.  */

int
nds32_hard_regno_nregs (int regno ATTRIBUTE_UNUSED,
			enum machine_mode mode)
{
  return ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD);
}

int
nds32_hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  /* Restrict double-word quantities to even register pairs.  */
  if (HARD_REGNO_NREGS (regno, mode) == 1
      || !((regno) & 1))
    return 1;

  return 0;
}


/* Register Classes.  */

enum reg_class
nds32_regno_reg_class (int regno)
{
  /* Refer to nds32.h for more register class details.  */

  if (regno >= 0 && regno <= 7)
    return LOW_REGS;
  else if (regno >= 8 && regno <= 11)
    return MIDDLE_REGS;
  else if (regno >= 12 && regno <= 14)
    return HIGH_REGS;
  else if (regno == 15)
    return R15_TA_REG;
  else if (regno >= 16 && regno <= 19)
    return MIDDLE_REGS;
  else if (regno >= 20 && regno <= 31)
    return HIGH_REGS;
  else if (regno == 32 || regno == 33)
    return FRAME_REGS;
  else
    return NO_REGS;
}


/* Stack Layout and Calling Conventions.  */

/* -- Basic Stack Layout.  */

rtx
nds32_return_addr_rtx (int count,
		       rtx frameaddr ATTRIBUTE_UNUSED)
{
  /* There is no way to determine the return address
     if frameaddr is the frame that has 'count' steps
     up from current frame.  */
  if (count != 0)
    return NULL_RTX;

  /* If count == 0, it means we are at current frame,
     the return address is $r30 ($lp).  */
  return get_hard_reg_initial_val (Pmode, LP_REGNUM);
}

/* -- Eliminating Frame Pointer and Arg Pointer.  */

HOST_WIDE_INT
nds32_initial_elimination_offset (unsigned int from_reg, unsigned int to_reg)
{
  HOST_WIDE_INT offset;

  /* Compute and setup stack frame size.
     The result will be in cfun->machine.  */
  nds32_compute_stack_frame ();

  /* Remember to consider
     cfun->machine->callee_saved_area_padding_bytes
     when calculating offset.  */
  if (from_reg == ARG_POINTER_REGNUM && to_reg == STACK_POINTER_REGNUM)
    {
      offset = (cfun->machine->fp_size
	        + cfun->machine->gp_size
		+ cfun->machine->lp_size
		+ cfun->machine->callee_saved_regs_size
		+ cfun->machine->callee_saved_area_padding_bytes
		+ cfun->machine->local_size
		+ cfun->machine->out_args_size);
    }
  else if (from_reg == ARG_POINTER_REGNUM
	   && to_reg == HARD_FRAME_POINTER_REGNUM)
    {
      offset = 0;
    }
  else if (from_reg == FRAME_POINTER_REGNUM
	   && to_reg == STACK_POINTER_REGNUM)
    {
      offset = (cfun->machine->local_size + cfun->machine->out_args_size);
    }
  else if (from_reg == FRAME_POINTER_REGNUM
	   && to_reg == HARD_FRAME_POINTER_REGNUM)
    {
      offset = (-1) * (cfun->machine->fp_size
		       + cfun->machine->gp_size
		       + cfun->machine->lp_size
		       + cfun->machine->callee_saved_regs_size
		       + cfun->machine->callee_saved_area_padding_bytes);
    }
  else
    {
      gcc_unreachable ();
    }

  return offset;
}

/* -- Passing Arguments in Registers.  */

void
nds32_init_cumulative_args (CUMULATIVE_ARGS *cum,
			    tree fntype ATTRIBUTE_UNUSED,
			    rtx libname ATTRIBUTE_UNUSED,
			    tree fndecl ATTRIBUTE_UNUSED,
			    int n_named_args ATTRIBUTE_UNUSED)
{
  /* Initial available registers
     (in offset, corresponding to NDS32_GPR_ARG_FIRST_REGNUM)
     for passing arguments.  */
  cum->reg_offset = 0;
}

/* -- Function Entry and Exit.  */

/* Function for normal multiple push prologue.  */
void
nds32_expand_prologue (void)
{
  int fp_adjust;
  int sp_adjust;
  int en4_const;

  rtx Rb, Re;
  rtx push_insn;
  rtx fp_adjust_insn, sp_adjust_insn;

  /* Before computing everything for stack frame size,
     we check if it is still worth to use fp_as_gp optimization.
     If it is, the 'df_regs_ever_live_p (FP_REGNUM)' will be set
     so that $fp will be saved on stack.  */
  cfun->machine->fp_as_gp_p = nds32_fp_as_gp_check_available ();

  /* Compute and setup stack frame size.
     The result will be in cfun->machine.  */
  nds32_compute_stack_frame ();

  /* If the function is 'naked',
     we do not have to generate prologue code fragment.  */
  if (cfun->machine->naked_p)
    return;

  /* Get callee_first_regno and callee_last_regno.  */
  Rb = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_first_regno);
  Re = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_last_regno);

  /* push_insn = gen_stack_push_multiple(first_regno, last_regno),
     the pattern 'stack_push_multiple' is implemented in nds32.md.
     For En4 field, we have to calculate its constant value.
     Refer to Andes ISA for more information.  */
  en4_const = 0;
  if (cfun->machine->fp_size)
    en4_const += 8;
  if (cfun->machine->gp_size)
    en4_const += 4;
  if (cfun->machine->lp_size)
    en4_const += 2;

  /* If $fp, $gp, $lp, and all callee-save registers are NOT required
     to be saved, we don't have to create multiple push instruction.
     Otherwise, a multiple push instruction is needed.  */
  if (!(REGNO (Rb) == SP_REGNUM && REGNO (Re) == SP_REGNUM && en4_const == 0))
    {
      /* Create multiple push instruction rtx.  */
      push_insn = nds32_gen_stack_push_multiple (Rb, Re, GEN_INT (en4_const));
      /* Emit rtx into instructions list and receive INSN rtx form.  */
      push_insn = emit_insn (push_insn);

      /* The insn rtx 'push_insn' will change frame layout.
         We need to use RTX_FRAME_RELATED_P so that GCC is able to
         generate CFI (Call Frame Information) stuff.  */
      RTX_FRAME_RELATED_P (push_insn) = 1;
    }

  /* Check frame_pointer_needed to see
     if we shall emit fp adjustment instruction.  */
  if (frame_pointer_needed)
    {
      /* adjust $fp = $sp + ($fp size) + ($gp size) + ($lp size)
                          + (4 * callee-saved-registers)
         Note: No need to adjust
               cfun->machine->callee_saved_area_padding_bytes,
               because, at this point, stack pointer is just
               at the position after push instruction.  */
      fp_adjust = cfun->machine->fp_size
		  + cfun->machine->gp_size
		  + cfun->machine->lp_size
		  + cfun->machine->callee_saved_regs_size;
      fp_adjust_insn = gen_addsi3 (hard_frame_pointer_rtx,
				   stack_pointer_rtx,
				   GEN_INT (fp_adjust));
      /* Emit rtx into instructions list and receive INSN rtx form.  */
      fp_adjust_insn = emit_insn (fp_adjust_insn);
    }

  /* Adjust $sp = $sp - local_size - out_args_size
                      - callee_saved_area_padding_bytes.  */
  sp_adjust = cfun->machine->local_size
	      + cfun->machine->out_args_size
	      + cfun->machine->callee_saved_area_padding_bytes;
  /* sp_adjust value may be out of range of the addi instruction,
     create alternative add behavior with TA_REGNUM if necessary,
     using NEGATIVE value to tell that we are decreasing address.  */
  sp_adjust = nds32_force_addi_stack_int ( (-1) * sp_adjust);
  if (sp_adjust)
    {
      /* Generate sp adjustment instruction if and only if sp_adjust != 0.  */
      sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
				   stack_pointer_rtx,
				   GEN_INT (-1 * sp_adjust));
      /* Emit rtx into instructions list and receive INSN rtx form.  */
      sp_adjust_insn = emit_insn (sp_adjust_insn);

      /* The insn rtx 'sp_adjust_insn' will change frame layout.
         We need to use RTX_FRAME_RELATED_P so that GCC is able to
         generate CFI (Call Frame Information) stuff.  */
      RTX_FRAME_RELATED_P (sp_adjust_insn) = 1;
    }

  /* Prevent the instruction scheduler from
     moving instructions across the boundary.  */
  emit_insn (gen_blockage ());
}

/* Function for normal multiple pop epilogue.  */
void
nds32_expand_epilogue (void)
{
  int sp_adjust;
  int en4_const;

  rtx Rb, Re;
  rtx pop_insn;
  rtx sp_adjust_insn;

  /* Compute and setup stack frame size.
     The result will be in cfun->machine.  */
  nds32_compute_stack_frame ();

  /* Prevent the instruction scheduler from
     moving instructions across the boundary.  */
  emit_insn (gen_blockage ());

  /* If the function is 'naked', we do not have to generate
     epilogue code fragment BUT 'ret' instruction.  */
  if (cfun->machine->naked_p)
    {
      /* Generate return instruction by using
         unspec_volatile_func_return pattern.
         Make sure this instruction is after gen_blockage().
         NOTE that $lp will become 'live'
         after this instruction has been emitted.  */
      emit_insn (gen_unspec_volatile_func_return ());
      return;
    }

  if (frame_pointer_needed)
    {
      /* adjust $sp = $fp - ($fp size) - ($gp size) - ($lp size)
                          - (4 * callee-saved-registers)
         Note: No need to adjust
               cfun->machine->callee_saved_area_padding_bytes,
               because we want to adjust stack pointer
               to the position for pop instruction.  */
      sp_adjust = cfun->machine->fp_size
		  + cfun->machine->gp_size
		  + cfun->machine->lp_size
		  + cfun->machine->callee_saved_regs_size;
      sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
				   hard_frame_pointer_rtx,
				   GEN_INT (-1 * sp_adjust));
      /* Emit rtx into instructions list and receive INSN rtx form.  */
      sp_adjust_insn = emit_insn (sp_adjust_insn);
    }
  else
    {
      /* If frame pointer is NOT needed,
         we cannot calculate the sp adjustment from frame pointer.
         Instead, we calculate the adjustment by local_size,
         out_args_size, and callee_saved_area_padding_bytes.
         Notice that such sp adjustment value may be out of range,
         so we have to deal with it as well.  */

      /* Adjust $sp = $sp + local_size + out_args_size
                          + callee_saved_area_padding_bytes.  */
      sp_adjust = cfun->machine->local_size
		  + cfun->machine->out_args_size
		  + cfun->machine->callee_saved_area_padding_bytes;
      /* sp_adjust value may be out of range of the addi instruction,
         create alternative add behavior with TA_REGNUM if necessary,
         using POSITIVE value to tell that we are increasing address.  */
      sp_adjust = nds32_force_addi_stack_int (sp_adjust);
      if (sp_adjust)
	{
	  /* Generate sp adjustment instruction
	     if and only if sp_adjust != 0.  */
	  sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
				       stack_pointer_rtx,
				       GEN_INT (sp_adjust));
	  /* Emit rtx into instructions list and receive INSN rtx form.  */
	  sp_adjust_insn = emit_insn (sp_adjust_insn);
	}
    }

  /* Get callee_first_regno and callee_last_regno.  */
  Rb = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_first_regno);
  Re = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_last_regno);

  /* pop_insn = gen_stack_pop_multiple(first_regno, last_regno),
     the pattern 'stack_pop_multiple' is implementad in nds32.md.
     For En4 field, we have to calculate its constant value.
     Refer to Andes ISA for more information.  */
  en4_const = 0;
  if (cfun->machine->fp_size)
    en4_const += 8;
  if (cfun->machine->gp_size)
    en4_const += 4;
  if (cfun->machine->lp_size)
    en4_const += 2;

  /* If $fp, $gp, $lp, and all callee-save registers are NOT required
     to be saved, we don't have to create multiple pop instruction.
     Otherwise, a multiple pop instruction is needed.  */
  if (!(REGNO (Rb) == SP_REGNUM && REGNO (Re) == SP_REGNUM && en4_const == 0))
    {
      /* Create multiple pop instruction rtx.  */
      pop_insn = nds32_gen_stack_pop_multiple (Rb, Re, GEN_INT (en4_const));
      /* Emit pop instruction.  */
      emit_insn (pop_insn);
    }

  /* Generate return instruction by using
     unspec_volatile_func_return pattern.  */
  emit_insn (gen_unspec_volatile_func_return ());
}

/* Function for v3push prologue.  */
void
nds32_expand_prologue_v3push (void)
{
  int fp_adjust;
  int sp_adjust;

  rtx Rb, Re;
  rtx push_insn;
  rtx fp_adjust_insn, sp_adjust_insn;

  /* Before computing everything for stack frame size,
     we check if it is still worth to use fp_as_gp optimization.
     If it is, the 'df_regs_ever_live_p (FP_REGNUM)' will be set
     so that $fp will be saved on stack.  */
  cfun->machine->fp_as_gp_p = nds32_fp_as_gp_check_available ();

  /* Compute and setup stack frame size.
     The result will be in cfun->machine.  */
  nds32_compute_stack_frame ();

  /* If the function is 'naked',
     we do not have to generate prologue code fragment.  */
  if (cfun->machine->naked_p)
    return;

  /* Get callee_first_regno and callee_last_regno.  */
  Rb = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_first_regno);
  Re = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_last_regno);

  /* Calculate sp_adjust first to test if 'push25 Re,imm8u' is available,
     where imm8u has to be 8-byte alignment.  */
  sp_adjust = cfun->machine->local_size
	      + cfun->machine->out_args_size
	      + cfun->machine->callee_saved_area_padding_bytes;

  if (satisfies_constraint_Iu08 (GEN_INT (sp_adjust))
      && NDS32_DOUBLE_WORD_ALIGN_P (sp_adjust))
    {
      /* We can use 'push25 Re,imm8u'.  */

      /* push_insn = gen_stack_v3push(last_regno, sp_adjust),
         the pattern 'stack_v3push' is implemented in nds32.md.
         The (const_int 14) means v3push always push { $fp $gp $lp }.  */
      push_insn = nds32_gen_stack_v3push (Rb, Re,
					  GEN_INT (14), GEN_INT (sp_adjust));
      /* emit rtx into instructions list and receive INSN rtx form */
      push_insn = emit_insn (push_insn);

      /* The insn rtx 'push_insn' will change frame layout.
         We need to use RTX_FRAME_RELATED_P so that GCC is able to
         generate CFI (Call Frame Information) stuff.  */
      RTX_FRAME_RELATED_P (push_insn) = 1;

      /* Check frame_pointer_needed to see
         if we shall emit fp adjustment instruction.  */
      if (frame_pointer_needed)
	{
	  /* adjust $fp = $sp   + 4         ($fp size)
	                        + 4         ($gp size)
	                        + 4         ($lp size)
	                        + (4 * n)   (callee-saved registers)
	                        + sp_adjust ('push25 Re,imm8u')
	     Note: Since we use 'push25 Re,imm8u',
	           the position of stack pointer is further
	           changed after push instruction.
	           Hence, we need to take sp_adjust value
	           into consideration.  */
	  fp_adjust = cfun->machine->fp_size
		      + cfun->machine->gp_size
		      + cfun->machine->lp_size
		      + cfun->machine->callee_saved_regs_size
		      + sp_adjust;
	  fp_adjust_insn = gen_addsi3 (hard_frame_pointer_rtx,
				       stack_pointer_rtx,
				       GEN_INT (fp_adjust));
	  /* Emit rtx into instructions list and receive INSN rtx form.  */
	  fp_adjust_insn = emit_insn (fp_adjust_insn);
	}
    }
  else
    {
      /* We have to use 'push25 Re,0' and
         expand one more instruction to adjust $sp later.  */

      /* push_insn = gen_stack_v3push(last_regno, sp_adjust),
         the pattern 'stack_v3push' is implemented in nds32.md.
         The (const_int 14) means v3push always push { $fp $gp $lp }.  */
      push_insn = nds32_gen_stack_v3push (Rb, Re,
					  GEN_INT (14), GEN_INT (0));
      /* Emit rtx into instructions list and receive INSN rtx form.  */
      push_insn = emit_insn (push_insn);

      /* The insn rtx 'push_insn' will change frame layout.
         We need to use RTX_FRAME_RELATED_P so that GCC is able to
         generate CFI (Call Frame Information) stuff.  */
      RTX_FRAME_RELATED_P (push_insn) = 1;

      /* Check frame_pointer_needed to see
         if we shall emit fp adjustment instruction.  */
      if (frame_pointer_needed)
	{
	  /* adjust $fp = $sp + 4        ($fp size)
	                      + 4        ($gp size)
	                      + 4        ($lp size)
	                      + (4 * n)  (callee-saved registers)
	     Note: Since we use 'push25 Re,0',
	           the stack pointer is just at the position
	           after push instruction.
	           No need to take sp_adjust into consideration.  */
	  fp_adjust = cfun->machine->fp_size
		      + cfun->machine->gp_size
		      + cfun->machine->lp_size
		      + cfun->machine->callee_saved_regs_size;
	  fp_adjust_insn = gen_addsi3 (hard_frame_pointer_rtx,
				       stack_pointer_rtx,
				       GEN_INT (fp_adjust));
	  /* Emit rtx into instructions list and receive INSN rtx form.  */
	  fp_adjust_insn = emit_insn (fp_adjust_insn);
	}

      /* Because we use 'push25 Re,0',
         we need to expand one more instruction to adjust $sp.
         However, sp_adjust value may be out of range of the addi instruction,
         create alternative add behavior with TA_REGNUM if necessary,
         using NEGATIVE value to tell that we are decreasing address.  */
      sp_adjust = nds32_force_addi_stack_int ( (-1) * sp_adjust);
      if (sp_adjust)
	{
	  /* Generate sp adjustment instruction
	     if and only if sp_adjust != 0.  */
	  sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
				       stack_pointer_rtx,
				       GEN_INT (-1 * sp_adjust));
	  /* Emit rtx into instructions list and receive INSN rtx form.  */
	  sp_adjust_insn = emit_insn (sp_adjust_insn);

	  /* The insn rtx 'sp_adjust_insn' will change frame layout.
	     We need to use RTX_FRAME_RELATED_P so that GCC is able to
	     generate CFI (Call Frame Information) stuff.  */
	  RTX_FRAME_RELATED_P (sp_adjust_insn) = 1;
	}
    }

  /* Prevent the instruction scheduler from
     moving instructions across the boundary.  */
  emit_insn (gen_blockage ());
}

/* Function for v3pop epilogue.  */
void
nds32_expand_epilogue_v3pop (void)
{
  int sp_adjust;

  rtx Rb, Re;
  rtx pop_insn;
  rtx sp_adjust_insn;

  /* Compute and setup stack frame size.
     The result will be in cfun->machine.  */
  nds32_compute_stack_frame ();

  /* Prevent the instruction scheduler from
     moving instructions across the boundary.  */
  emit_insn (gen_blockage ());

  /* If the function is 'naked', we do not have to generate
     epilogue code fragment BUT 'ret' instruction.  */
  if (cfun->machine->naked_p)
    {
      /* Generate return instruction by using
         unspec_volatile_func_return pattern.
         Make sure this instruction is after gen_blockage().
         NOTE that $lp will become 'live'
         after this instruction has been emitted.  */
      emit_insn (gen_unspec_volatile_func_return ());
      return;
    }

  /* Get callee_first_regno and callee_last_regno.  */
  Rb = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_first_regno);
  Re = gen_rtx_REG (SImode, cfun->machine->callee_saved_regs_last_regno);

  /* Calculate sp_adjust first to test if 'pop25 Re,imm8u' is available,
     where imm8u has to be 8-byte alignment.  */
  sp_adjust = cfun->machine->local_size
	      + cfun->machine->out_args_size
	      + cfun->machine->callee_saved_area_padding_bytes;

  /* We have to consider alloca issue as well.
     If the function does call alloca(), the stack pointer is not fixed.
     In that case, we cannot use 'pop25 Re,imm8u' directly.
     We have to caculate stack pointer from frame pointer
     and then use 'pop25 Re,0'.
     Of course, the frame_pointer_needed should be nonzero
     if the function calls alloca().  */
  if (satisfies_constraint_Iu08 (GEN_INT (sp_adjust))
      && NDS32_DOUBLE_WORD_ALIGN_P (sp_adjust)
      && !cfun->calls_alloca)
    {
      /* We can use 'pop25 Re,imm8u'.  */

      /* pop_insn = gen_stack_v3pop(last_regno, sp_adjust),
         the pattern 'stack_v3pop' is implementad in nds32.md.
         The (const_int 14) means v3pop always pop { $fp $gp $lp }.  */
      pop_insn = nds32_gen_stack_v3pop (Rb, Re,
					GEN_INT (14), GEN_INT (sp_adjust));

      /* Emit pop instruction.  */
      emit_insn (pop_insn);
    }
  else
    {
      /* We have to use 'pop25 Re,0', and prior to it,
         we must expand one more instruction to adjust $sp.  */

      if (frame_pointer_needed)
	{
	  /* adjust $sp = $fp - 4        ($fp size)
	                      - 4        ($gp size)
	                      - 4        ($lp size)
	                      - (4 * n)  (callee-saved registers)
	     Note: No need to adjust
	           cfun->machine->callee_saved_area_padding_bytes,
	           because we want to adjust stack pointer
	           to the position for pop instruction.  */
	  sp_adjust = cfun->machine->fp_size
		      + cfun->machine->gp_size
		      + cfun->machine->lp_size
		      + cfun->machine->callee_saved_regs_size;
	  sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
				       hard_frame_pointer_rtx,
				       GEN_INT (-1 * sp_adjust));
	  /* Emit rtx into instructions list and receive INSN rtx form.  */
	  sp_adjust_insn = emit_insn (sp_adjust_insn);
	}
      else
	{
	  /* If frame pointer is NOT needed,
	     we cannot calculate the sp adjustment from frame pointer.
	     Instead, we calculate the adjustment by local_size,
	     out_args_size, and callee_saved_area_padding_bytes.
	     Notice that such sp adjustment value may be out of range,
	     so we have to deal with it as well.  */

	  /* Adjust $sp = $sp + local_size + out_args_size
			      + callee_saved_area_padding_bytes.  */
	  sp_adjust = cfun->machine->local_size
		      + cfun->machine->out_args_size
		      + cfun->machine->callee_saved_area_padding_bytes;
	  /* sp_adjust value may be out of range of the addi instruction,
	     create alternative add behavior with TA_REGNUM if necessary,
	     using POSITIVE value to tell that we are increasing address.  */
	  sp_adjust = nds32_force_addi_stack_int (sp_adjust);
	  if (sp_adjust)
	    {
	      /* Generate sp adjustment instruction
	         if and only if sp_adjust != 0.  */
	      sp_adjust_insn = gen_addsi3 (stack_pointer_rtx,
					   stack_pointer_rtx,
					   GEN_INT (sp_adjust));
	      /* Emit rtx into instructions list and receive INSN rtx form.  */
	      sp_adjust_insn = emit_insn (sp_adjust_insn);
	    }
	}

      /* pop_insn = gen_stack_v3pop(last_regno, sp_adjust),
         the pattern 'stack_v3pop' is implementad in nds32.md.  */
      /* The (const_int 14) means v3pop always pop { $fp $gp $lp }.  */
      pop_insn = nds32_gen_stack_v3pop (Rb, Re,
					GEN_INT (14), GEN_INT (0));

      /* Emit pop instruction.  */
      emit_insn (pop_insn);
    }
}

/* ------------------------------------------------------------------------ */

/* Function to test 333-form for load/store instructions.
   This is auxiliary extern function for auxiliary macro in nds32.h.
   Because it is a little complicated, we use function instead of macro.  */
bool
nds32_ls_333_p (rtx rt, rtx ra, rtx imm, enum machine_mode mode)
{
  if (REGNO_REG_CLASS (REGNO (rt)) == LOW_REGS
      && REGNO_REG_CLASS (REGNO (ra)) == LOW_REGS)
    {
      if (GET_MODE_SIZE (mode) == 4)
	return satisfies_constraint_Iu05 (imm);

      if (GET_MODE_SIZE (mode) == 2)
	return satisfies_constraint_Iu04 (imm);

      if (GET_MODE_SIZE (mode) == 1)
	return satisfies_constraint_Iu03 (imm);
    }

  return false;
}


/* Computing the Length of an Insn.
   Modifies the length assigned to instruction INSN.
   LEN is the initially computed length of the insn.  */
int
nds32_adjust_insn_length (rtx insn, int length)
{
  rtx src, dst;

  switch (recog_memoized (insn))
    {
    case CODE_FOR_move_df:
    case CODE_FOR_move_di:
      /* Adjust length of movd44 to 2.  */
      src = XEXP (PATTERN (insn), 1);
      dst = XEXP (PATTERN (insn), 0);

      if (REG_P (src)
	  && REG_P (dst)
	  && (REGNO (src) % 2) == 0
	  && (REGNO (dst) % 2) == 0)
	length = 2;
      break;

    default:
      break;
    }

  return length;
}


/* Return align 2 (log base 2) if the next instruction of LABEL is 4 byte.  */
int
nds32_target_alignment (rtx label)
{
  rtx insn;

  if (optimize_size)
    return 0;

  insn = next_active_insn (label);

  if (insn == 0)
    return 0;
  else if ((get_attr_length (insn) % 4) == 0)
    return 2;
  else
    return 0;
}

/* ------------------------------------------------------------------------ */

/* PART 5: Initialize target hook structure and definitions.  */

/* Controlling the Compilation Driver.  */


/* Run-time Target Specification.  */


/* Defining Data Structures for Per-function Information.  */


/* Storage Layout.  */

#undef TARGET_PROMOTE_FUNCTION_MODE
#define TARGET_PROMOTE_FUNCTION_MODE \
  default_promote_function_mode_always_promote


/* Layout of Source Language Data Types.  */


/* Register Usage.  */

/* -- Basic Characteristics of Registers.  */

/* -- Order of Allocation of Registers.  */

/* -- How Values Fit in Registers.  */

/* -- Handling Leaf Functions.  */

/* -- Registers That Form a Stack.  */


/* Register Classes.  */

#undef TARGET_CLASS_MAX_NREGS
#define TARGET_CLASS_MAX_NREGS nds32_class_max_nregs

#undef TARGET_LRA_P
#define TARGET_LRA_P hook_bool_void_true

#undef TARGET_REGISTER_PRIORITY
#define TARGET_REGISTER_PRIORITY nds32_register_priority


/* Obsolete Macros for Defining Constraints.  */


/* Stack Layout and Calling Conventions.  */

/* -- Basic Stack Layout.  */

/* -- Exception Handling Support.  */

/* -- Specifying How Stack Checking is Done.  */

/* -- Registers That Address the Stack Frame.  */

/* -- Eliminating Frame Pointer and Arg Pointer.  */

#undef TARGET_CAN_ELIMINATE
#define TARGET_CAN_ELIMINATE nds32_can_eliminate

/* -- Passing Function Arguments on the Stack.  */

/* -- Passing Arguments in Registers.  */

#undef TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG nds32_function_arg

#undef TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE nds32_function_arg_advance

#undef TARGET_FUNCTION_ARG_BOUNDARY
#define TARGET_FUNCTION_ARG_BOUNDARY nds32_function_arg_boundary

/* -- How Scalar Function Values Are Returned.  */

#undef TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE nds32_function_value

#undef TARGET_LIBCALL_VALUE
#define TARGET_LIBCALL_VALUE nds32_libcall_value

#undef TARGET_FUNCTION_VALUE_REGNO_P
#define TARGET_FUNCTION_VALUE_REGNO_P nds32_function_value_regno_p

/* -- How Large Values Are Returned.  */

/* -- Caller-Saves Register Allocation.  */

/* -- Function Entry and Exit.  */

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE nds32_asm_function_prologue

#undef TARGET_ASM_FUNCTION_END_PROLOGUE
#define TARGET_ASM_FUNCTION_END_PROLOGUE nds32_asm_function_end_prologue

#undef  TARGET_ASM_FUNCTION_BEGIN_EPILOGUE
#define TARGET_ASM_FUNCTION_BEGIN_EPILOGUE nds32_asm_function_begin_epilogue

#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE nds32_asm_function_epilogue

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK nds32_asm_output_mi_thunk

#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

/* -- Generating Code for Profiling.  */

/* -- Permitting tail calls.  */

#undef TARGET_WARN_FUNC_RETURN
#define TARGET_WARN_FUNC_RETURN nds32_warn_func_return

/* Stack smashing protection.  */


/* Implementing the Varargs Macros.  */

#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING nds32_strict_argument_naming


/* Trampolines for Nested Functions.  */

#undef TARGET_ASM_TRAMPOLINE_TEMPLATE
#define TARGET_ASM_TRAMPOLINE_TEMPLATE nds32_asm_trampoline_template

#undef TARGET_TRAMPOLINE_INIT
#define TARGET_TRAMPOLINE_INIT nds32_trampoline_init


/* Implicit Calls to Library Routines.  */


/* Addressing Modes.  */

#undef TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P nds32_legitimate_address_p


/* Anchored Addresses.  */


/* Condition Code Status.  */

/* -- Representation of condition codes using (cc0).  */

/* -- Representation of condition codes using registers.  */

/* -- Macros to control conditional execution.  */


/* Describing Relative Costs of Operations.  */

#undef TARGET_REGISTER_MOVE_COST
#define TARGET_REGISTER_MOVE_COST nds32_register_move_cost

#undef TARGET_MEMORY_MOVE_COST
#define TARGET_MEMORY_MOVE_COST nds32_memory_move_cost

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS nds32_rtx_costs

#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST nds32_address_cost


/* Adjusting the Instruction Scheduler.  */


/* Dividing the Output into Sections (Texts, Data, . . . ).  */


/* Position Independent Code.  */


/* Defining the Output Assembler Language.  */

/* -- The Overall Framework of an Assembler File.  */

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START nds32_asm_file_start
#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END nds32_asm_file_end

/* -- Output of Data.  */

#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.hword\t"

#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"

/* -- Output of Uninitialized Variables.  */

/* -- Output and Generation of Labels.  */

#undef TARGET_ASM_GLOBALIZE_LABEL
#define TARGET_ASM_GLOBALIZE_LABEL nds32_asm_globalize_label

/* -- How Initialization Functions Are Handled.  */

/* -- Macros Controlling Initialization Routines.  */

/* -- Output of Assembler Instructions.  */

#undef TARGET_PRINT_OPERAND
#define TARGET_PRINT_OPERAND nds32_print_operand
#undef TARGET_PRINT_OPERAND_ADDRESS
#define TARGET_PRINT_OPERAND_ADDRESS nds32_print_operand_address

/* -- Output of Dispatch Tables.  */

/* -- Assembler Commands for Exception Regions.  */

/* -- Assembler Commands for Alignment.  */


/* Controlling Debugging Information Format.  */

/* -- Macros Affecting All Debugging Formats.  */

/* -- Specific Options for DBX Output.  */

/* -- Open-Ended Hooks for DBX Format.  */

/* -- File Names in DBX Format.  */

/* -- Macros for SDB and DWARF Output.  */

/* -- Macros for VMS Debug Format.  */


/* Cross Compilation and Floating Point.  */


/* Mode Switching Instructions.  */


/* Defining target-specific uses of __attribute__.  */

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE nds32_attribute_table

#undef TARGET_MERGE_DECL_ATTRIBUTES
#define TARGET_MERGE_DECL_ATTRIBUTES nds32_merge_decl_attributes

#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES nds32_insert_attributes

#undef TARGET_OPTION_PRAGMA_PARSE
#define TARGET_OPTION_PRAGMA_PARSE nds32_option_pragma_parse

#undef TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE nds32_option_override


/* Emulating TLS.  */


/* Defining coprocessor specifics for MIPS targets.  */


/* Parameters for Precompiled Header Validity Checking.  */


/* C++ ABI parameters.  */


/* Adding support for named address spaces.  */


/* Miscellaneous Parameters.  */

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS nds32_init_builtins

#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN nds32_expand_builtin


/* ------------------------------------------------------------------------ */

/* Initialize the GCC target structure.  */

struct gcc_target targetm = TARGET_INITIALIZER;

/* ------------------------------------------------------------------------ */
