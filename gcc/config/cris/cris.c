/* Definitions for GCC.  Part of the machine description for CRIS.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Axis Communications.  Written by Hans-Peter Nilsson.

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
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "flags.h"
#include "tree.h"
#include "expr.h"
#include "except.h"
#include "function.h"
#include "toplev.h"
#include "recog.h"
#include "tm_p.h"
#include "debug.h"
#include "output.h"
#include "target.h"
#include "target-def.h"
#include "ggc.h"
#include "optabs.h"

/* Usable when we have an amount to add or subtract, and want the
   optimal size of the insn.  */
#define ADDITIVE_SIZE_MODIFIER(size) \
 ((size) <= 63 ? "q" : (size) <= 255 ? "u.b" : (size) <= 65535 ? "u.w" : ".d")

#define ASSERT_PLT_UNSPEC(x)					\
  do								\
    {								\
      if (XINT (x, 1) != CRIS_UNSPEC_PLT			\
	  || (GET_CODE (XVECEXP (x, 0, 0)) != SYMBOL_REF	\
	      && GET_CODE (XVECEXP (x, 0, 0)) != LABEL_REF))	\
	abort ();						\
    } while (0)

#define LOSE_AND_RETURN(msgid, x)			\
  do						\
    {						\
      cris_operand_lossage (msgid, x);		\
      return;					\
    } while (0)

enum cris_retinsn_type
 { CRIS_RETINSN_UNKNOWN = 0, CRIS_RETINSN_RET, CRIS_RETINSN_JUMP };

/* Per-function machine data.  */
struct machine_function GTY(())
 {
   int needs_return_address_on_stack;

   /* This is the number of registers we save in the prologue due to
      stdarg.  */
   int stdarg_regs;

   enum cris_retinsn_type return_type;
 };

/* This little fix suppresses the 'u' or 's' when '%e' in assembly
   pattern.  */
static char cris_output_insn_is_bound = 0;

/* This one suppresses printing out the "rPIC+" in
   "rPIC+sym:GOTOFF+offset" when doing PIC.  For a PLT symbol, it
   suppresses outputting it as [rPIC+sym:GOTPLT] and outputs similarly
   just the "sym:GOTOFF" part.  */
static int cris_pic_sympart_only = 0;

/* In code for output macros, this is how we know whether e.g. constant
   goes in code or in a static initializer.  */
static int in_code = 0;

/* Fix for reg_overlap_mentioned_p.  */
static int cris_reg_overlap_mentioned_p (rtx, rtx);

static void cris_print_base (rtx, FILE *);

static void cris_print_index (rtx, FILE *);

static void cris_output_addr_const (FILE *, rtx);

static struct machine_function * cris_init_machine_status (void);

static rtx cris_struct_value_rtx (tree, int);

static void cris_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode,
					 tree type, int *, int);

static int cris_initial_frame_pointer_offset (void);

static int saved_regs_mentioned (rtx);

static void cris_operand_lossage (const char *, rtx);

static int cris_reg_saved_in_regsave_area  (unsigned int, bool);

static void cris_asm_output_mi_thunk
  (FILE *, tree, HOST_WIDE_INT, HOST_WIDE_INT, tree);

static void cris_file_start (void);
static void cris_init_libfuncs (void);

static bool cris_rtx_costs (rtx, int, int, int *);
static int cris_address_cost (rtx);
static bool cris_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				    tree, bool);
static int cris_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode,
				   tree, bool);
static tree cris_md_asm_clobbers (tree, tree, tree);

static bool cris_handle_option (size_t, const char *, int);

/* This is the argument from the "-max-stack-stackframe=" option.  */
const char *cris_max_stackframe_str;

/* This is the argument from the "-march=" option.  */
const char *cris_cpu_str;

/* This is the argument from the "-mtune=" option.  */
const char *cris_tune_str;

/* This is the argument from the "-melinux-stacksize=" option.  */
const char *cris_elinux_stacksize_str;

/* This is the parsed result of the "-max-stack-stackframe=" option.  If
   it (still) is zero, then there was no such option given.  */
int cris_max_stackframe = 0;

/* This is the parsed result of the "-march=" option, if given.  */
int cris_cpu_version = CRIS_DEFAULT_CPU_VERSION;

#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.dword\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.quad\t"

/* We need to define these, since the 2byte, 4byte, 8byte op:s are only
   available in ELF.  These "normal" pseudos do not have any alignment
   constraints or side-effects.  */
#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP TARGET_ASM_ALIGNED_HI_OP

#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP TARGET_ASM_ALIGNED_SI_OP

#undef TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP TARGET_ASM_ALIGNED_DI_OP

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK cris_asm_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START cris_file_start

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS cris_init_libfuncs

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS cris_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST cris_address_cost

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX cris_struct_value_rtx
#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS cris_setup_incoming_varargs
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE cris_pass_by_reference
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES cris_arg_partial_bytes
#undef TARGET_MD_ASM_CLOBBERS
#define TARGET_MD_ASM_CLOBBERS cris_md_asm_clobbers
#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (TARGET_DEFAULT | CRIS_SUBTARGET_DEFAULT)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION cris_handle_option

struct gcc_target targetm = TARGET_INITIALIZER;

/* Helper for cris_load_multiple_op and cris_ret_movem_op.  */

bool
cris_movem_load_rest_p (rtx op, int offs)
{
  unsigned int reg_count = XVECLEN (op, 0) - offs;
  rtx src_addr;
  int i;
  rtx elt;
  int setno;
  int regno_dir = 1;
  unsigned int regno = 0;

  /* Perform a quick check so we don't blow up below.  FIXME: Adjust for
     other than (MEM reg).  */
  if (reg_count <= 1
      || GET_CODE (XVECEXP (op, 0, offs)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, offs))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, offs))) != MEM)
    return false;

  /* Check a possible post-inc indicator.  */
  if (GET_CODE (SET_SRC (XVECEXP (op, 0, offs + 1))) == PLUS)
    {
      rtx reg = XEXP (SET_SRC (XVECEXP (op, 0, offs + 1)), 0);
      rtx inc = XEXP (SET_SRC (XVECEXP (op, 0, offs + 1)), 1);

      reg_count--;

      if (reg_count == 1
	  || !REG_P (reg)
	  || !REG_P (SET_DEST (XVECEXP (op, 0, offs + 1)))
	  || REGNO (reg) != REGNO (SET_DEST (XVECEXP (op, 0, offs + 1)))
	  || GET_CODE (inc) != CONST_INT
	  || INTVAL (inc) != (HOST_WIDE_INT) reg_count * 4)
	return false;
      i = offs + 2;
    }
  else
    i = offs + 1;

  /* FIXME: These two only for pre-v32.  */
  regno_dir = -1;
  regno = reg_count - 1;

  elt = XVECEXP (op, 0, offs);
  src_addr = XEXP (SET_SRC (elt), 0);

  if (GET_CODE (elt) != SET
      || GET_CODE (SET_DEST (elt)) != REG
      || GET_MODE (SET_DEST (elt)) != SImode
      || REGNO (SET_DEST (elt)) != regno
      || GET_CODE (SET_SRC (elt)) != MEM
      || GET_MODE (SET_SRC (elt)) != SImode
      || !memory_address_p (SImode, src_addr))
    return false;

  for (setno = 1; i < XVECLEN (op, 0); setno++, i++)
    {
      rtx elt = XVECEXP (op, 0, i);
      regno += regno_dir;

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || REGNO (SET_DEST (elt)) != regno
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || GET_MODE (SET_SRC (elt)) != SImode
	  || GET_CODE (XEXP (SET_SRC (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_SRC (elt), 0), 0), src_addr)
	  || GET_CODE (XEXP (XEXP (SET_SRC (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_SRC (elt), 0), 1)) != setno * 4)
	return false;
    }

  return true;
}

/* Worker function for predicate for the parallel contents in a movem
   to-memory.  */

bool
cris_store_multiple_op_p (rtx op)
{
  int reg_count = XVECLEN (op, 0);
  rtx dest;
  rtx dest_addr;
  rtx dest_base;
  int i;
  rtx elt;
  int setno;
  int regno_dir = 1;
  int regno = 0;
  int offset = 0;

  /* Perform a quick check so we don't blow up below.  FIXME: Adjust for
     other than (MEM reg) and (MEM (PLUS reg const)).  */
  if (reg_count <= 1)
    return false;

  elt = XVECEXP (op, 0, 0);

  if (GET_CODE (elt) != SET)
    return  false;

  dest = SET_DEST (elt);

  if (GET_CODE (SET_SRC (elt)) != REG
      || GET_CODE (dest) != MEM)
    return false;

  dest_addr = XEXP (dest, 0);

  /* Check a possible post-inc indicator.  */
  if (GET_CODE (SET_SRC (XVECEXP (op, 0, 1))) == PLUS)
    {
      rtx reg = XEXP (SET_SRC (XVECEXP (op, 0, 1)), 0);
      rtx inc = XEXP (SET_SRC (XVECEXP (op, 0, 1)), 1);

      reg_count--;

      if (reg_count == 1
	  || !REG_P (reg)
	  || !REG_P (SET_DEST (XVECEXP (op, 0, 1)))
	  || REGNO (reg) != REGNO (SET_DEST (XVECEXP (op, 0, 1)))
	  || GET_CODE (inc) != CONST_INT
	  /* Support increment by number of registers, and by the offset
	     of the destination, if it has the form (MEM (PLUS reg
	     offset)).  */
	  || !((REG_P (dest_addr)
		&& REGNO (dest_addr) == REGNO (reg)
		&& INTVAL (inc) == (HOST_WIDE_INT) reg_count * 4)
	       || (GET_CODE (dest_addr) == PLUS
		   && REG_P (XEXP (dest_addr, 0))
		   && REGNO (XEXP (dest_addr, 0)) == REGNO (reg)
		   && GET_CODE (XEXP (dest_addr, 1)) == CONST_INT
		   && INTVAL (XEXP (dest_addr, 1)) == INTVAL (inc))))
	return false;

      i = 2;
    }
  else
    i = 1;

  /* FIXME: These two only for pre-v32.  */
  regno_dir = -1;
  regno = reg_count - 1;

  if (GET_CODE (elt) != SET
      || GET_CODE (SET_SRC (elt)) != REG
      || GET_MODE (SET_SRC (elt)) != SImode
      || REGNO (SET_SRC (elt)) != (unsigned int) regno
      || GET_CODE (SET_DEST (elt)) != MEM
      || GET_MODE (SET_DEST (elt)) != SImode)
    return false;

  if (REG_P (dest_addr))
    {
      dest_base = dest_addr;
      offset = 0;
    }
  else if (GET_CODE (dest_addr) == PLUS
	   && REG_P (XEXP (dest_addr, 0))
	   && GET_CODE (XEXP (dest_addr, 1)) == CONST_INT)
    {
      dest_base = XEXP (dest_addr, 0);
      offset = INTVAL (XEXP (dest_addr, 1));
    }
  else
    return false;

  for (setno = 1; i < XVECLEN (op, 0); setno++, i++)
    {
      rtx elt = XVECEXP (op, 0, i);
      regno += regno_dir;

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != SImode
	  || REGNO (SET_SRC (elt)) != (unsigned int) regno
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || GET_CODE (XEXP (SET_DEST (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_DEST (elt), 0), 0), dest_base)
	  || GET_CODE (XEXP (XEXP (SET_DEST (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_DEST (elt), 0), 1)) != setno * 4 + offset)
	return false;
    }

  return true;
}

/* The CONDITIONAL_REGISTER_USAGE worker.  */

void
cris_conditional_register_usage (void)
{
  /* FIXME: This isn't nice.  We should be able to use that register for
     something else if the PIC table isn't needed.  */
  if (flag_pic)
    fixed_regs[PIC_OFFSET_TABLE_REGNUM]
      = call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;

  if (TARGET_HAS_MUL_INSNS)
    fixed_regs[CRIS_MOF_REGNUM] = 0;
}

/* Return current_function_uses_pic_offset_table.  For use in cris.md,
   since some generated files do not include function.h.  */

int
cris_cfun_uses_pic_table (void)
{
  return current_function_uses_pic_offset_table;
}

/* Given an rtx, return the text string corresponding to the CODE of X.
   Intended for use in the assembly language output section of a
   define_insn.  */

const char *
cris_op_str (rtx x)
{
  cris_output_insn_is_bound = 0;
  switch (GET_CODE (x))
    {
    case PLUS:
      return "add";
      break;

    case MINUS:
      return "sub";
      break;

    case MULT:
      /* This function is for retrieving a part of an instruction name for
	 an operator, for immediate output.  If that ever happens for
	 MULT, we need to apply TARGET_MUL_BUG in the caller.  Make sure
	 we notice.  */
      abort ();
      break;

    case DIV:
      return "div";
      break;

    case AND:
      return "and";
      break;

    case IOR:
      return "or";
      break;

    case XOR:
      return "xor";
      break;

    case NOT:
      return "not";
      break;

    case ASHIFT:
      return "lsl";
      break;

    case LSHIFTRT:
      return "lsr";
      break;

    case ASHIFTRT:
      return "asr";
      break;

    case UMIN:
      /* Used to control the sign/zero-extend character for the 'e' modifier.
	 BOUND has none.  */
      cris_output_insn_is_bound = 1;
      return "bound";
      break;

    default:
      return "Unknown operator";
      break;
  }
}

/* Emit an error message when we're in an asm, and a fatal error for
   "normal" insns.  Formatted output isn't easily implemented, since we
   use output_operand_lossage to output the actual message and handle the
   categorization of the error.  */

static void
cris_operand_lossage (const char *msgid, rtx op)
{
  debug_rtx (op);
  output_operand_lossage ("%s", msgid);
}

/* Print an index part of an address to file.  */

static void
cris_print_index (rtx index, FILE *file)
{
  rtx inner = XEXP (index, 0);

  /* Make the index "additive" unless we'll output a negative number, in
     which case the sign character is free (as in free beer).  */
  if (GET_CODE (index) != CONST_INT || INTVAL (index) >= 0)
    putc ('+', file);

  if (REG_P (index))
    fprintf (file, "$%s.b", reg_names[REGNO (index)]);
  else if (CONSTANT_P (index))
    cris_output_addr_const (file, index);
  else if (GET_CODE (index) == MULT)
    {
      fprintf (file, "$%s.",
	       reg_names[REGNO (XEXP (index, 0))]);

      putc (INTVAL (XEXP (index, 1)) == 2 ? 'w' : 'd', file);
    }
  else if (GET_CODE (index) == SIGN_EXTEND &&
	   GET_CODE (inner) == MEM)
    {
      rtx inner_inner = XEXP (inner, 0);

      if (GET_CODE (inner_inner) == POST_INC)
	{
	  fprintf (file, "[$%s+].",
		   reg_names[REGNO (XEXP (inner_inner, 0))]);
	  putc (GET_MODE (inner) == HImode ? 'w' : 'b', file);
	}
      else
	{
	  fprintf (file, "[$%s].", reg_names[REGNO (inner_inner)]);

	  putc (GET_MODE (inner) == HImode ? 'w' : 'b', file);
	}
    }
  else if (GET_CODE (index) == MEM)
    {
      if (GET_CODE (inner) == POST_INC)
	fprintf (file, "[$%s+].d", reg_names[REGNO (XEXP (inner, 0))]);
      else
	fprintf (file, "[$%s].d", reg_names[REGNO (inner)]);
    }
  else
    cris_operand_lossage ("unexpected index-type in cris_print_index",
			  index);
}

/* Print a base rtx of an address to file.  */

static void
cris_print_base (rtx base, FILE *file)
{
  if (REG_P (base))
    fprintf (file, "$%s", reg_names[REGNO (base)]);
  else if (GET_CODE (base) == POST_INC)
    fprintf (file, "$%s+", reg_names[REGNO (XEXP (base, 0))]);
  else
    cris_operand_lossage ("unexpected base-type in cris_print_base",
			  base);
}

/* Usable as a guard in expressions.  */

int
cris_fatal (char *arg)
{
  internal_error (arg);

  /* We'll never get here; this is just to appease compilers.  */
  return 0;
}

/* Return nonzero if REGNO is an ordinary register that *needs* to be
   saved together with other registers, possibly by a MOVEM instruction,
   or is saved for target-independent reasons.  There may be
   target-dependent reasons to save the register anyway; this is just a
   wrapper for a complicated conditional.  */

static int
cris_reg_saved_in_regsave_area (unsigned int regno, bool got_really_used)
{
  return
    (((regs_ever_live[regno]
       && !call_used_regs[regno])
      || (regno == PIC_OFFSET_TABLE_REGNUM
	  && (got_really_used
	      /* It is saved anyway, if there would be a gap.  */
	      || (flag_pic
		  && regs_ever_live[regno + 1]
		  && !call_used_regs[regno + 1]))))
     && (regno != FRAME_POINTER_REGNUM || !frame_pointer_needed)
     && regno != CRIS_SRP_REGNUM)
    || (current_function_calls_eh_return
	&& (regno == EH_RETURN_DATA_REGNO (0)
	    || regno == EH_RETURN_DATA_REGNO (1)
	    || regno == EH_RETURN_DATA_REGNO (2)
	    || regno == EH_RETURN_DATA_REGNO (3)));
}

/* Return nonzero if there are regs mentioned in the insn that are not all
   in the call_used regs.  This is part of the decision whether an insn
   can be put in the epilogue.  */

static int
saved_regs_mentioned (rtx x)
{
  int i;
  const char *fmt;
  RTX_CODE code;

  /* Mainly stolen from refers_to_regno_p in rtlanal.c.  */

  code = GET_CODE (x);

  switch (code)
    {
    case REG:
      i = REGNO (x);
      return !call_used_regs[i];

    case SUBREG:
      /* If this is a SUBREG of a hard reg, we can see exactly which
	 registers are being modified.  Otherwise, handle normally.  */
      i = REGNO (SUBREG_REG (x));
      return !call_used_regs[i];

    default:
      ;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  if (saved_regs_mentioned (XEXP (x, i)))
	    return 1;
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >=0; j--)
	    if (saved_regs_mentioned (XEXP (x, i)))
	      return 1;
	}
    }

  return 0;
}

/* The PRINT_OPERAND worker.  */

void
cris_print_operand (FILE *file, rtx x, int code)
{
  rtx operand = x;

  /* Size-strings corresponding to MULT expressions.  */
  static const char *const mults[] = { "BAD:0", ".b", ".w", "BAD:3", ".d" };

  /* New code entries should just be added to the switch below.  If
     handling is finished, just return.  If handling was just a
     modification of the operand, the modified operand should be put in
     "operand", and then do a break to let default handling
     (zero-modifier) output the operand.  */

  switch (code)
    {
    case 'b':
      /* Print the unsigned supplied integer as if it were signed
	 and < 0, i.e print 255 or 65535 as -1, 254, 65534 as -2, etc.  */
      if (GET_CODE (x) != CONST_INT
	  || ! CONST_OK_FOR_LETTER_P (INTVAL (x), 'O'))
	LOSE_AND_RETURN ("invalid operand for 'b' modifier", x);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC,
	       INTVAL (x)| (INTVAL (x) <= 255 ? ~255 : ~65535));
      return;

    case 'x':
      /* Print assembler code for operator.  */
      fprintf (file, "%s", cris_op_str (operand));
      return;

    case 'v':
      /* Print the operand without the PIC register.  */
      if (! flag_pic || ! CONSTANT_P (x) || ! cris_gotless_symbol (x))
	LOSE_AND_RETURN ("invalid operand for 'v' modifier", x);
      cris_pic_sympart_only++;
      cris_output_addr_const (file, x);
      cris_pic_sympart_only--;
      return;

    case 'o':
      {
	/* A movem modifier working on a parallel; output the register
	   name.  */
	int regno;

	if (GET_CODE (x) != PARALLEL)
	  LOSE_AND_RETURN ("invalid operand for 'o' modifier", x);

	/* The second item can be (set reg (plus reg const)) to denote a
	   postincrement.  */
	regno
	  = (GET_CODE (SET_SRC (XVECEXP (x, 0, 1))) == PLUS
	     ? XVECLEN (x, 0) - 2
	     : XVECLEN (x, 0) - 1);

	fprintf (file, "$%s", reg_names [regno]);
      }
      return;

    case 'O':
      {
	/* A similar movem modifier; output the memory operand.  */
	rtx addr;

	if (GET_CODE (x) != PARALLEL)
	  LOSE_AND_RETURN ("invalid operand for 'O' modifier", x);

	/* The lowest mem operand is in the first item, but perhaps it
	   needs to be output as postincremented.  */
	addr = GET_CODE (SET_SRC (XVECEXP (x, 0, 0))) == MEM
	  ? XEXP (SET_SRC (XVECEXP (x, 0, 0)), 0)
	  : XEXP (SET_DEST (XVECEXP (x, 0, 0)), 0);

	/* The second item can be a (set reg (plus reg const)) to denote
	   a modification.  */
	if (GET_CODE (SET_SRC (XVECEXP (x, 0, 1))) == PLUS)
	  {
	    /* It's a post-increment, if the address is a naked (reg).  */
	    if (REG_P (addr))
	      addr = gen_rtx_POST_INC (SImode, addr);
	    else
	      {
		/* Otherwise, it's a side-effect; RN=RN+M.  */
		fprintf (file, "[$%s=$%s%s%d]",
			 reg_names [REGNO (SET_DEST (XVECEXP (x, 0, 1)))],
			 reg_names [REGNO (XEXP (addr, 0))],
			 INTVAL (XEXP (addr, 1)) < 0 ? "" : "+",
			 (int) INTVAL (XEXP (addr, 1)));
		return;
	      }
	  }
	output_address (addr);
      }
      return;

    case 'P':
      /* Print the PIC register.  Applied to a GOT-less PIC symbol for
         sanity.  */
      if (! flag_pic || ! CONSTANT_P (x) || ! cris_gotless_symbol (x))
	LOSE_AND_RETURN ("invalid operand for 'P' modifier", x);
      fprintf (file, "$%s", reg_names [PIC_OFFSET_TABLE_REGNUM]);
      return;

    case 'p':
      /* Adjust a power of two to its log2.  */
      if (GET_CODE (x) != CONST_INT || exact_log2 (INTVAL (x)) < 0 )
	LOSE_AND_RETURN ("invalid operand for 'p' modifier", x);
      fprintf (file, "%d", exact_log2 (INTVAL (x)));
      return;

    case 's':
      /* For an integer, print 'b' or 'w' if <= 255 or <= 65535
	 respectively.  This modifier also terminates the inhibiting
         effects of the 'x' modifier.  */
      cris_output_insn_is_bound = 0;
      if (GET_MODE (x) == VOIDmode && GET_CODE (x) == CONST_INT)
	{
	  if (INTVAL (x) >= 0)
	    {
	      if (INTVAL (x) <= 255)
		putc ('b', file);
	      else if (INTVAL (x) <= 65535)
		putc ('w', file);
	      else
		putc ('d', file);
	    }
	  else
	    putc ('d', file);
	  return;
	}

      /* For a non-integer, print the size of the operand.  */
      putc ((GET_MODE (x) == SImode || GET_MODE (x) == SFmode)
	    ? 'd' : GET_MODE (x) == HImode ? 'w'
	    : GET_MODE (x) == QImode ? 'b'
	    /* If none of the above, emit an erroneous size letter.  */
	    : 'X',
	    file);
      return;

    case 'z':
      /* Const_int: print b for -127 <= x <= 255,
	 w for -32768 <= x <= 65535, else abort.  */
      if (GET_CODE (x) != CONST_INT
	  || INTVAL (x) < -32768 || INTVAL (x) > 65535)
	LOSE_AND_RETURN ("invalid operand for 'z' modifier", x);
      putc (INTVAL (x) >= -128 && INTVAL (x) <= 255 ? 'b' : 'w', file);
      return;

    case '#':
      /* Output a 'nop' if there's nothing for the delay slot.
	 This method stolen from the sparc files.  */
      if (dbr_sequence_length () == 0)
	fputs ("\n\tnop", file);
      return;

    case '!':
      /* Output directive for alignment padded with "nop" insns.
	 Optimizing for size, it's plain 4-byte alignment, otherwise we
	 align the section to a cache-line (32 bytes) and skip at max 2
	 bytes, i.e. we skip if it's the last insn on a cache-line.  The
	 latter is faster by a small amount (for two test-programs 99.6%
	 and 99.9%) and larger by a small amount (ditto 100.1% and
	 100.2%).  This is supposed to be the simplest yet performance-
	 wise least intrusive way to make sure the immediately following
	 (supposed) muls/mulu insn isn't located at the end of a
	 cache-line.  */
      if (TARGET_MUL_BUG)
	fputs (optimize_size
	       ? ".p2alignw 2,0x050f\n\t"
	       : ".p2alignw 5,0x050f,2\n\t", file);
      return;

    case 'H':
      /* Print high (most significant) part of something.  */
      switch (GET_CODE (operand))
	{
	case CONST_INT:
	  if (HOST_BITS_PER_WIDE_INT == 32)
	    /* Sign-extension from a normal int to a long long.  */
	    fprintf (file, INTVAL (operand) < 0 ? "-1" : "0");
	  else
	    fprintf (file, "0x%x", (unsigned int)(INTVAL (x) >> 31 >> 1));
	  return;

	case CONST_DOUBLE:
	  /* High part of a long long constant.  */
	  if (GET_MODE (operand) == VOIDmode)
	    {
	      fprintf (file, HOST_WIDE_INT_PRINT_HEX, CONST_DOUBLE_HIGH (x));
	      return;
	    }
	  else
	    LOSE_AND_RETURN ("invalid operand for 'H' modifier", x);

	case REG:
	  /* Print reg + 1.  Check that there's not an attempt to print
	     high-parts of registers like stack-pointer or higher.  */
	  if (REGNO (operand) > STACK_POINTER_REGNUM - 2)
	    LOSE_AND_RETURN ("bad register", operand);
	  fprintf (file, "$%s", reg_names[REGNO (operand) + 1]);
	  return;

	case MEM:
	  /* Adjust memory address to high part.  */
	  {
	    rtx adj_mem = operand;
	    int size
	      = GET_MODE_BITSIZE (GET_MODE (operand)) / BITS_PER_UNIT;

	    /* Adjust so we can use two SImode in DImode.
	       Calling adj_offsettable_operand will make sure it is an
	       offsettable address.  Don't do this for a postincrement
	       though; it should remain as it was.  */
	    if (GET_CODE (XEXP (adj_mem, 0)) != POST_INC)
	      adj_mem
		= adjust_address (adj_mem, GET_MODE (adj_mem), size / 2);

	    output_address (XEXP (adj_mem, 0));
	    return;
	  }

	default:
	  LOSE_AND_RETURN ("invalid operand for 'H' modifier", x);
	}

    case 'L':
      /* Strip the MEM expression.  */
      operand = XEXP (operand, 0);
      break;

    case 'e':
      /* Print 's' if operand is SIGN_EXTEND or 'u' if ZERO_EXTEND unless
	 cris_output_insn_is_bound is nonzero.  */
      if (GET_CODE (operand) != SIGN_EXTEND
	  && GET_CODE (operand) != ZERO_EXTEND
	  && GET_CODE (operand) != CONST_INT)
	LOSE_AND_RETURN ("invalid operand for 'e' modifier", x);

      if (cris_output_insn_is_bound)
	{
	  cris_output_insn_is_bound = 0;
	  return;
	}

      putc (GET_CODE (operand) == SIGN_EXTEND
	    || (GET_CODE (operand) == CONST_INT && INTVAL (operand) < 0)
	    ? 's' : 'u', file);
      return;

    case 'm':
      /* Print the size letter of the inner element.  We can do it by
	 calling ourselves with the 's' modifier.  */
      if (GET_CODE (operand) != SIGN_EXTEND && GET_CODE (operand) != ZERO_EXTEND)
	LOSE_AND_RETURN ("invalid operand for 'm' modifier", x);
      cris_print_operand (file, XEXP (operand, 0), 's');
      return;

    case 'M':
      /* Print the least significant part of operand.  */
      if (GET_CODE (operand) == CONST_DOUBLE)
	{
	  fprintf (file, HOST_WIDE_INT_PRINT_HEX, CONST_DOUBLE_LOW (x));
	  return;
	}
      else if (HOST_BITS_PER_WIDE_INT > 32 && GET_CODE (operand) == CONST_INT)
	{
	  fprintf (file, HOST_WIDE_INT_PRINT_HEX,
		   INTVAL (x) & ((unsigned int) 0x7fffffff * 2 + 1));
	  return;
	}
      /* Otherwise the least significant part equals the normal part,
	 so handle it normally.  */
      break;

    case 'A':
      /* When emitting an add for the high part of a DImode constant, we
	 want to use addq for 0 and adds.w for -1.  */
      if (GET_CODE (operand) != CONST_INT)
	LOSE_AND_RETURN ("invalid operand for 'A' modifier", x);
      fprintf (file, INTVAL (operand) < 0 ? "adds.w" : "addq");
      return;

    case 'd':
      /* If this is a GOT symbol, print it as :GOT regardless of -fpic.  */
      if (flag_pic && CONSTANT_P (operand) && cris_got_symbol (operand))
	{
	  cris_output_addr_const (file, operand);
	  fprintf (file, ":GOT");
	  return;
	}
      break;

    case 'D':
      /* When emitting an sub for the high part of a DImode constant, we
	 want to use subq for 0 and subs.w for -1.  */
      if (GET_CODE (operand) != CONST_INT)
	LOSE_AND_RETURN ("invalid operand for 'D' modifier", x);
      fprintf (file, INTVAL (operand) < 0 ? "subs.w" : "subq");
      return;

    case 'S':
      /* Print the operand as the index-part of an address.
	 Easiest way out is to use cris_print_index.  */
      cris_print_index (operand, file);
      return;

    case 'T':
      /* Print the size letter for an operand to a MULT, which must be a
	 const_int with a suitable value.  */
      if (GET_CODE (operand) != CONST_INT || INTVAL (operand) > 4)
	LOSE_AND_RETURN ("invalid operand for 'T' modifier", x);
      fprintf (file, "%s", mults[INTVAL (operand)]);
      return;

    case 0:
      /* No code, print as usual.  */
      break;

    default:
      LOSE_AND_RETURN ("invalid operand modifier letter", x);
    }

  /* Print an operand as without a modifier letter.  */
  switch (GET_CODE (operand))
    {
    case REG:
      if (REGNO (operand) > 15
	  && REGNO (operand) != CRIS_MOF_REGNUM
	  && REGNO (operand) != CRIS_SRP_REGNUM)
	internal_error ("internal error: bad register: %d", REGNO (operand));
      fprintf (file, "$%s", reg_names[REGNO (operand)]);
      return;

    case MEM:
      output_address (XEXP (operand, 0));
      return;

    case CONST_DOUBLE:
      if (GET_MODE (operand) == VOIDmode)
	/* A long long constant.  */
	output_addr_const (file, operand);
      else
	{
	  /* Only single precision is allowed as plain operands the
	     moment.  FIXME:  REAL_VALUE_FROM_CONST_DOUBLE isn't
	     documented.  */
	  REAL_VALUE_TYPE r;
	  long l;

	  /* FIXME:  Perhaps check overflow of the "single".  */
	  REAL_VALUE_FROM_CONST_DOUBLE (r, operand);
	  REAL_VALUE_TO_TARGET_SINGLE (r, l);

	  fprintf (file, "0x%lx", l);
	}
      return;

    case UNSPEC:
      ASSERT_PLT_UNSPEC (operand);
      /* Fall through.  */

    case CONST:
      cris_output_addr_const (file, operand);
      return;

    case MULT:
    case ASHIFT:
      {
	/* For a (MULT (reg X) const_int) we output "rX.S".  */
	int i = GET_CODE (XEXP (operand, 1)) == CONST_INT
	  ? INTVAL (XEXP (operand, 1)) : INTVAL (XEXP (operand, 0));
	rtx reg = GET_CODE (XEXP (operand, 1)) == CONST_INT
	  ? XEXP (operand, 0) : XEXP (operand, 1);

	if (GET_CODE (reg) != REG
	    || (GET_CODE (XEXP (operand, 0)) != CONST_INT
		&& GET_CODE (XEXP (operand, 1)) != CONST_INT))
	  LOSE_AND_RETURN ("unexpected multiplicative operand", x);

	cris_print_base (reg, file);
	fprintf (file, ".%c",
		 i == 0 || (i == 1 && GET_CODE (operand) == MULT) ? 'b'
		 : i == 4 ? 'd'
		 : (i == 2 && GET_CODE (operand) == MULT) || i == 1 ? 'w'
		 : 'd');
	return;
      }

    default:
      /* No need to handle all strange variants, let output_addr_const
	 do it for us.  */
      if (CONSTANT_P (operand))
	{
	  cris_output_addr_const (file, operand);
	  return;
	}

      LOSE_AND_RETURN ("unexpected operand", x);
    }
}

/* The PRINT_OPERAND_ADDRESS worker.  */

void
cris_print_operand_address (FILE *file, rtx x)
{
  /* All these were inside MEM:s so output indirection characters.  */
  putc ('[', file);

  if (CONSTANT_ADDRESS_P (x))
    cris_output_addr_const (file, x);
  else if (BASE_OR_AUTOINCR_P (x))
    cris_print_base (x, file);
  else if (GET_CODE (x) == PLUS)
    {
      rtx x1, x2;

      x1 = XEXP (x, 0);
      x2 = XEXP (x, 1);
      if (BASE_P (x1))
	{
	  cris_print_base (x1, file);
	  cris_print_index (x2, file);
	}
      else if (BASE_P (x2))
	{
	  cris_print_base (x2, file);
	  cris_print_index (x1, file);
	}
      else
	LOSE_AND_RETURN ("unrecognized address", x);
    }
  else if (GET_CODE (x) == MEM)
    {
      /* A DIP.  Output more indirection characters.  */
      putc ('[', file);
      cris_print_base (XEXP (x, 0), file);
      putc (']', file);
    }
  else
    LOSE_AND_RETURN ("unrecognized address", x);

  putc (']', file);
}

/* The RETURN_ADDR_RTX worker.
   We mark that the return address is used, either by EH or
   __builtin_return_address, for use by the function prologue and
   epilogue.  FIXME: This isn't optimal; we just use the mark in the
   prologue and epilogue to say that the return address is to be stored
   in the stack frame.  We could return SRP for leaf-functions and use the
   initial-value machinery.  */

rtx
cris_return_addr_rtx (int count, rtx frameaddr ATTRIBUTE_UNUSED)
{
  cfun->machine->needs_return_address_on_stack = 1;

  /* The return-address is stored just above the saved frame-pointer (if
     present).  Apparently we can't eliminate from the frame-pointer in
     that direction, so use the incoming args (maybe pretended) pointer.  */
  return count == 0
    ? gen_rtx_MEM (Pmode, plus_constant (virtual_incoming_args_rtx, -4))
    : NULL_RTX;
}

/* Accessor used in cris.md:return because cfun->machine isn't available
   there.  */

bool
cris_return_address_on_stack (void)
{
  return regs_ever_live[CRIS_SRP_REGNUM]
    || cfun->machine->needs_return_address_on_stack;
}

/* Accessor used in cris.md:return because cfun->machine isn't available
   there.  */

bool
cris_return_address_on_stack_for_return (void)
{
  return cfun->machine->return_type == CRIS_RETINSN_RET ? false
    : cris_return_address_on_stack ();
}

/* This used to be the INITIAL_FRAME_POINTER_OFFSET worker; now only
   handles FP -> SP elimination offset.  */

static int
cris_initial_frame_pointer_offset (void)
{
  int regno;

  /* Initial offset is 0 if we don't have a frame pointer.  */
  int offs = 0;
  bool got_really_used = current_function_uses_pic_offset_table;

  /* And 4 for each register pushed.  */
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (cris_reg_saved_in_regsave_area (regno, got_really_used))
      offs += 4;

  /* And then, last, we add the locals allocated.  */
  offs += get_frame_size ();

  /* And more; the accumulated args size.  */
  offs += current_function_outgoing_args_size;

  /* Then round it off, in case we use aligned stack.  */
  if (TARGET_STACK_ALIGN)
    offs = TARGET_ALIGN_BY_32 ? (offs + 3) & ~3 : (offs + 1) & ~1;

  return offs;
}

/* The INITIAL_ELIMINATION_OFFSET worker.
   Calculate the difference between imaginary registers such as frame
   pointer and the stack pointer.  Used to eliminate the frame pointer
   and imaginary arg pointer.  */

int
cris_initial_elimination_offset (int fromreg, int toreg)
{
  int fp_sp_offset
    = cris_initial_frame_pointer_offset ();

  /* We should be able to use regs_ever_live and related prologue
     information here, or alpha should not as well.  */
  bool return_address_on_stack = cris_return_address_on_stack ();

  /* Here we act as if the frame-pointer were needed.  */
  int ap_fp_offset = 4 + (return_address_on_stack ? 4 : 0);

  if (fromreg == ARG_POINTER_REGNUM
      && toreg == FRAME_POINTER_REGNUM)
    return ap_fp_offset;

  /* Between the frame pointer and the stack are only "normal" stack
     variables and saved registers.  */
  if (fromreg == FRAME_POINTER_REGNUM
      && toreg == STACK_POINTER_REGNUM)
    return fp_sp_offset;

  /* We need to balance out the frame pointer here.  */
  if (fromreg == ARG_POINTER_REGNUM
      && toreg == STACK_POINTER_REGNUM)
    return ap_fp_offset + fp_sp_offset - 4;

  abort ();
}

/*  This function looks into the pattern to see how this insn affects
    condition codes.

    Used when to eliminate test insns before a condition-code user,
    such as a "scc" insn or a conditional branch.  This includes
    checking if the entities that cc was updated by, are changed by the
    operation.

    Currently a jumble of the old peek-inside-the-insn and the newer
    check-cc-attribute methods.  */

void
cris_notice_update_cc (rtx exp, rtx insn)
{
  /* Check if user specified "-mcc-init" as a bug-workaround.  FIXME:
     TARGET_CCINIT does not work; we must set CC_REVERSED as below.
     Several testcases will otherwise fail, for example
     gcc.c-torture/execute/20000217-1.c -O0 and -O1.  */
  if (TARGET_CCINIT)
    {
      CC_STATUS_INIT;
      return;
    }

  /* Slowly, we're converting to using attributes to control the setting
     of condition-code status.  */
  switch (get_attr_cc (insn))
    {
    case CC_NONE:
      /* Even if it is "none", a setting may clobber a previous
	 cc-value, so check.  */
      if (GET_CODE (exp) == SET)
	{
	  if (cc_status.value1
	      && modified_in_p (cc_status.value1, insn))
	    cc_status.value1 = 0;

	  if (cc_status.value2
	      && modified_in_p (cc_status.value2, insn))
	    cc_status.value2 = 0;
	}
      return;

    case CC_CLOBBER:
      CC_STATUS_INIT;
      break;

    case CC_NORMAL:
      /* Which means, for:
	 (set (cc0) (...)):
	 CC is (...).

	 (set (reg) (...)):
	 CC is (reg) and (...) - unless (...) is 0, then CC does not change.
	 CC_NO_OVERFLOW unless (...) is reg or mem.

	 (set (mem) (...)):
	 CC does not change.

	 (set (pc) (...)):
	 CC does not change.

	 (parallel
	  (set (reg1) (mem (bdap/biap)))
	  (set (reg2) (bdap/biap))):
	 CC is (reg1) and (mem (reg2))

	 (parallel
	  (set (mem (bdap/biap)) (reg1)) [or 0]
	  (set (reg2) (bdap/biap))):
	 CC does not change.

	 (where reg and mem includes strict_low_parts variants thereof)

	 For all others, assume CC is clobbered.
	 Note that we do not have to care about setting CC_NO_OVERFLOW,
	 since the overflow flag is set to 0 (i.e. right) for
	 instructions where it does not have any sane sense, but where
	 other flags have meanings.  (This includes shifts; the carry is
	 not set by them).

	 Note that there are other parallel constructs we could match,
	 but we don't do that yet.  */

      if (GET_CODE (exp) == SET)
	{
	  /* FIXME: Check when this happens.  It looks like we should
	     actually do a CC_STATUS_INIT here to be safe.  */
	  if (SET_DEST (exp) == pc_rtx)
	    return;

	  /* Record CC0 changes, so we do not have to output multiple
	     test insns.  */
	  if (SET_DEST (exp) == cc0_rtx)
	    {
	      cc_status.value1 = SET_SRC (exp);
	      cc_status.value2 = 0;

	      /* Handle flags for the special btstq on one bit.  */
	      if (GET_CODE (SET_SRC (exp)) == ZERO_EXTRACT
		  && XEXP (SET_SRC (exp), 1) == const1_rtx)
		{
		  if (GET_CODE (XEXP (SET_SRC (exp), 0)) == CONST_INT)
		    /* Using cmpq.  */
		    cc_status.flags = CC_INVERTED;
		  else
		    /* A one-bit btstq.  */
		    cc_status.flags = CC_Z_IN_NOT_N;
		}
	      else
		cc_status.flags = 0;

	      if (GET_CODE (SET_SRC (exp)) == COMPARE)
		{
		  if (!REG_P (XEXP (SET_SRC (exp), 0))
		      && XEXP (SET_SRC (exp), 1) != const0_rtx)
		    /* For some reason gcc will not canonicalize compare
		       operations, reversing the sign by itself if
		       operands are in wrong order.  */
		    /* (But NOT inverted; eq is still eq.) */
		    cc_status.flags = CC_REVERSED;

		  /* This seems to be overlooked by gcc.  FIXME: Check again.
		     FIXME:  Is it really safe?  */
		  cc_status.value2
		    = gen_rtx_MINUS (GET_MODE (SET_SRC (exp)),
				     XEXP (SET_SRC (exp), 0),
				     XEXP (SET_SRC (exp), 1));
		}
	      return;
	    }
	  else if (REG_P (SET_DEST (exp))
		   || (GET_CODE (SET_DEST (exp)) == STRICT_LOW_PART
		       && REG_P (XEXP (SET_DEST (exp), 0))))
	    {
	      /* A register is set; normally CC is set to show that no
		 test insn is needed.  Catch the exceptions.  */

	      /* If not to cc0, then no "set"s in non-natural mode give
		 ok cc0...  */
	      if (GET_MODE_SIZE (GET_MODE (SET_DEST (exp))) > UNITS_PER_WORD
		  || GET_MODE_CLASS (GET_MODE (SET_DEST (exp))) == MODE_FLOAT)
		{
		  /* ... except add:s and sub:s in DImode.  */
		  if (GET_MODE (SET_DEST (exp)) == DImode
		      && (GET_CODE (SET_SRC (exp)) == PLUS
			  || GET_CODE (SET_SRC (exp)) == MINUS))
		    {
		      cc_status.flags = 0;
		      cc_status.value1 = SET_DEST (exp);
		      cc_status.value2 = SET_SRC (exp);

		      if (cris_reg_overlap_mentioned_p (cc_status.value1,
							cc_status.value2))
			cc_status.value2 = 0;

		      /* Add and sub may set V, which gets us
			 unoptimizable results in "gt" and "le" condition
			 codes.  */
		      cc_status.flags |= CC_NO_OVERFLOW;

		      return;
		    }
		}
	      else if (SET_SRC (exp) == const0_rtx)
		{
		  /* There's no CC0 change when clearing a register or
		     memory.  Just check for overlap.  */
		  if (cc_status.value1
		      && modified_in_p (cc_status.value1, insn))
		    cc_status.value1 = 0;

		  if (cc_status.value2
		      && modified_in_p (cc_status.value2, insn))
		    cc_status.value2 = 0;

		  return;
		}
	      else
		{
		  cc_status.flags = 0;
		  cc_status.value1 = SET_DEST (exp);
		  cc_status.value2 = SET_SRC (exp);

		  if (cris_reg_overlap_mentioned_p (cc_status.value1,
						    cc_status.value2))
		    cc_status.value2 = 0;

		  /* Some operations may set V, which gets us
		     unoptimizable results in "gt" and "le" condition
		     codes.  */
		  if (GET_CODE (SET_SRC (exp)) == PLUS
		      || GET_CODE (SET_SRC (exp)) == MINUS
		      || GET_CODE (SET_SRC (exp)) == NEG)
		    cc_status.flags |= CC_NO_OVERFLOW;

		  return;
		}
	    }
	  else if (GET_CODE (SET_DEST (exp)) == MEM
		   || (GET_CODE (SET_DEST (exp)) == STRICT_LOW_PART
		       && GET_CODE (XEXP (SET_DEST (exp), 0)) == MEM))
	    {
	      /* When SET to MEM, then CC is not changed (except for
		 overlap).  */
	      if (cc_status.value1
		  && modified_in_p (cc_status.value1, insn))
		cc_status.value1 = 0;

	      if (cc_status.value2
		  && modified_in_p (cc_status.value2, insn))
		cc_status.value2 = 0;

	      return;
	    }
	}
      else if (GET_CODE (exp) == PARALLEL)
	{
	  if (GET_CODE (XVECEXP (exp, 0, 0)) == SET
	      && GET_CODE (XVECEXP (exp, 0, 1)) == SET
	      && REG_P (XEXP (XVECEXP (exp, 0, 1), 0)))
	    {
	      if (REG_P (XEXP (XVECEXP (exp, 0, 0), 0))
		  && GET_CODE (XEXP (XVECEXP (exp, 0, 0), 1)) == MEM)
		{
		  /* For "move.S [rx=ry+o],rz", say CC reflects
		     value1=rz and value2=[rx] */
		  cc_status.value1 = XEXP (XVECEXP (exp, 0, 0), 0);
		  cc_status.value2
		    = replace_equiv_address (XEXP (XVECEXP (exp, 0, 0), 1),
					     XEXP (XVECEXP (exp, 0, 1), 0));
		  cc_status.flags = 0;

		  /* Huh?  A side-effect cannot change the destination
		     register.  */
		  if (cris_reg_overlap_mentioned_p (cc_status.value1,
						    cc_status.value2))
		    internal_error ("internal error: sideeffect-insn affecting main effect");
		  return;
		}
	      else if ((REG_P (XEXP (XVECEXP (exp, 0, 0), 1))
			|| XEXP (XVECEXP (exp, 0, 0), 1) == const0_rtx)
		       && GET_CODE (XEXP (XVECEXP (exp, 0, 0), 0)) == MEM)
		{
		  /* For "move.S rz,[rx=ry+o]" and "clear.S [rx=ry+o]",
		     say flags are not changed, except for overlap.  */
		  if (cc_status.value1
		      && modified_in_p (cc_status.value1, insn))
		    cc_status.value1 = 0;

		  if (cc_status.value2
		      && modified_in_p (cc_status.value2, insn))
		    cc_status.value2 = 0;

		  return;
		}
	    }
	}
      break;

    default:
      /* Unknown cc_attr value.  */
      abort ();
    }

  CC_STATUS_INIT;
}

/* Return != 0 if the return sequence for the current function is short,
   like "ret" or "jump [sp+]".  Prior to reloading, we can't tell if
   registers must be saved, so return 0 then.  */

bool
cris_simple_epilogue (void)
{
  unsigned int regno;
  unsigned int reglimit = STACK_POINTER_REGNUM;
  bool got_really_used = current_function_uses_pic_offset_table;

  if (! reload_completed
      || frame_pointer_needed
      || get_frame_size () != 0
      || current_function_pretend_args_size
      || current_function_args_size
      || current_function_outgoing_args_size
      || current_function_calls_eh_return

      /* If we're not supposed to emit prologue and epilogue, we must
	 not emit return-type instructions.  */
      || !TARGET_PROLOGUE_EPILOGUE)
    return false;

  /* No simple epilogue if there are saved registers.  */
  for (regno = 0; regno < reglimit; regno++)
    if (cris_reg_saved_in_regsave_area (regno, got_really_used))
      return false;

  return true;
}

/* Expand a return insn (just one insn) marked as using SRP or stack
   slot depending on parameter ON_STACK.  */

void
cris_expand_return (bool on_stack)
{
  /* FIXME: emit a parallel with a USE for SRP or the stack-slot, to
     tell "ret" from "jump [sp+]".  Some, but not all, other parts of
     GCC expect just (return) to do the right thing when optimizing, so
     we do that until they're fixed.  Currently, all return insns in a
     function must be the same (not really a limiting factor) so we need
     to check that it doesn't change half-way through.  */
  emit_jump_insn (gen_rtx_RETURN (VOIDmode));

  if ((cfun->machine->return_type == CRIS_RETINSN_RET && on_stack)
      || (cfun->machine->return_type == CRIS_RETINSN_JUMP && !on_stack))
    abort ();

  cfun->machine->return_type
    = on_stack ? CRIS_RETINSN_JUMP : CRIS_RETINSN_RET;
}

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
cris_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST_INT:
      {
	HOST_WIDE_INT val = INTVAL (x);
	if (val == 0)
	  *total = 0;
	else if (val < 32 && val >= -32)
	  *total = 1;
	/* Eight or 16 bits are a word and cycle more expensive.  */
	else if (val <= 32767 && val >= -32768)
	  *total = 2;
	/* A 32 bit constant (or very seldom, unsigned 16 bits) costs
	   another word.  FIXME: This isn't linear to 16 bits.  */
	else
	  *total = 4;
	return true;
      }

    case LABEL_REF:
      *total = 6;
      return true;

    case CONST:
    case SYMBOL_REF:
      /* For PIC, we need a prefix (if it isn't already there),
	 and the PIC register.  For a global PIC symbol, we also
	 need a read of the GOT.  */
      if (flag_pic)
	{
	  if (cris_got_symbol (x))
	    *total = 2 + 4 + 6;
	  else
	    *total = 2 + 6;
	}
      else
	*total = 6;
      return true;

    case CONST_DOUBLE:
      if (x != CONST0_RTX (GET_MODE (x) == VOIDmode ? DImode : GET_MODE (x)))
	*total = 12;
      else
        /* Make 0.0 cheap, else test-insns will not be used.  */
	*total = 0;
      return true;

    case MULT:
      /* Identify values that are no powers of two.  Powers of 2 are
         taken care of already and those values should not be changed.  */
      if (GET_CODE (XEXP (x, 1)) != CONST_INT
          || exact_log2 (INTVAL (XEXP (x, 1)) < 0))
	{
	  /* If we have a multiply insn, then the cost is between
	     1 and 2 "fast" instructions.  */
	  if (TARGET_HAS_MUL_INSNS)
	    {
	      *total = COSTS_N_INSNS (1) + COSTS_N_INSNS (1) / 2;
	      return true;
	    }

	  /* Estimate as 4 + 4 * #ofbits.  */
	  *total = COSTS_N_INSNS (132);
	  return true;
	}
      return false;

    case UDIV:
    case MOD:
    case UMOD:
    case DIV:
      if (GET_CODE (XEXP (x, 1)) != CONST_INT
          || exact_log2 (INTVAL (XEXP (x, 1)) < 0))
	{
	  /* Estimate this as 4 + 8 * #of bits.  */
	  *total = COSTS_N_INSNS (260);
	  return true;
	}
      return false;

    case AND:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
          /* Two constants may actually happen before optimization.  */
          && GET_CODE (XEXP (x, 0)) != CONST_INT
          && !CONST_OK_FOR_LETTER_P (INTVAL (XEXP (x, 1)), 'I'))
	{
	  *total = (rtx_cost (XEXP (x, 0), outer_code) + 2
		    + 2 * GET_MODE_NUNITS (GET_MODE (XEXP (x, 0))));
	  return true;
	}
      return false;

    case ZERO_EXTEND: case SIGN_EXTEND:
      *total = rtx_cost (XEXP (x, 0), outer_code);
      return true;

    default:
      return false;
    }
}

/* The ADDRESS_COST worker.  */

static int
cris_address_cost (rtx x)
{
  /* The metric to use for the cost-macros is unclear.
     The metric used here is (the number of cycles needed) / 2,
     where we consider equal a cycle for a word of code and a cycle to
     read memory.  */

  /* The cheapest addressing modes get 0, since nothing extra is needed.  */
  if (BASE_OR_AUTOINCR_P (x))
    return 0;

  /* An indirect mem must be a DIP.  This means two bytes extra for code,
     and 4 bytes extra for memory read, i.e.  (2 + 4) / 2.  */
  if (GET_CODE (x) == MEM)
    return (2 + 4) / 2;

  /* Assume (2 + 4) / 2 for a single constant; a dword, since it needs
     an extra DIP prefix and 4 bytes of constant in most cases.
     For PIC and a symbol with a GOT entry, we double the cost since we
     add a [rPIC+...] offset.  A GOT-less symbol uses a BDAP prefix
     equivalent to the DIP prefix for non-PIC, hence the same cost.  */
  if (CONSTANT_P (x))
    return flag_pic && cris_got_symbol (x) ? 2 * (2 + 4) / 2 : (2 + 4) / 2;

  /* Handle BIAP and BDAP prefixes.  */
  if (GET_CODE (x) == PLUS)
    {
      rtx tem1 = XEXP (x, 0);
      rtx tem2 = XEXP (x, 1);

    /* A BIAP is 2 extra bytes for the prefix insn, nothing more.  We
       recognize the typical MULT which is always in tem1 because of
       insn canonicalization.  */
    if ((GET_CODE (tem1) == MULT && BIAP_INDEX_P (tem1))
	|| REG_P (tem1))
      return 2 / 2;

    /* A BDAP (quick) is 2 extra bytes.  Any constant operand to the
       PLUS is always found in tem2.  */
    if (GET_CODE (tem2) == CONST_INT
	&& INTVAL (tem2) < 128 && INTVAL (tem2) >= -128)
      return 2 / 2;

    /* A BDAP -32768 .. 32767 is like BDAP quick, but with 2 extra
       bytes.  */
    if (GET_CODE (tem2) == CONST_INT
	&& CONST_OK_FOR_LETTER_P (INTVAL (tem2), 'L'))
      return (2 + 2) / 2;

    /* A BDAP with some other constant is 2 bytes extra.  */
    if (CONSTANT_P (tem2))
      return (2 + 2 + 2) / 2;

    /* BDAP with something indirect should have a higher cost than
       BIAP with register.   FIXME: Should it cost like a MEM or more?  */
    /* Don't need to check it, it's the only one left.
       FIXME:  There was a REG test missing, perhaps there are others.
       Think more.  */
    return (2 + 2 + 2) / 2;
  }

  /* What else?  Return a high cost.  It matters only for valid
     addressing modes.  */
  return 10;
}

/* Check various objections to the side-effect.  Used in the test-part
   of an anonymous insn describing an insn with a possible side-effect.
   Returns nonzero if the implied side-effect is ok.

   code     : PLUS or MULT
   ops	    : An array of rtx:es. lreg, rreg, rval,
	      The variables multop and other_op are indexes into this,
	      or -1 if they are not applicable.
   lreg     : The register that gets assigned in the side-effect.
   rreg     : One register in the side-effect expression
   rval     : The other register, or an int.
   multop   : An integer to multiply rval with.
   other_op : One of the entities of the main effect,
	      whose mode we must consider.  */

int
cris_side_effect_mode_ok (enum rtx_code code, rtx *ops,
			  int lreg, int rreg, int rval,
			  int multop, int other_op)
{
  /* Find what value to multiply with, for rx =ry + rz * n.  */
  int mult = multop < 0 ? 1 : INTVAL (ops[multop]);

  rtx reg_rtx = ops[rreg];
  rtx val_rtx = ops[rval];

  /* The operands may be swapped.  Canonicalize them in reg_rtx and
     val_rtx, where reg_rtx always is a reg (for this constraint to
     match).  */
  if (! BASE_P (reg_rtx))
    reg_rtx = val_rtx, val_rtx = ops[rreg];

  /* Don't forget to check that reg_rtx really is a reg.  If it isn't,
     we have no business.  */
  if (! BASE_P (reg_rtx))
    return 0;

  /* Don't do this when -mno-split.  */
  if (!TARGET_SIDE_EFFECT_PREFIXES)
    return 0;

  /* The mult expression may be hidden in lreg.  FIXME:  Add more
     commentary about that.  */
  if (GET_CODE (val_rtx) == MULT)
    {
      mult = INTVAL (XEXP (val_rtx, 1));
      val_rtx = XEXP (val_rtx, 0);
      code = MULT;
    }

  /* First check the "other operand".  */
  if (other_op >= 0)
    {
      if (GET_MODE_SIZE (GET_MODE (ops[other_op])) > UNITS_PER_WORD)
	return 0;

      /* Check if the lvalue register is the same as the "other
	 operand".  If so, the result is undefined and we shouldn't do
	 this.  FIXME:  Check again.  */
      if ((BASE_P (ops[lreg])
	   && BASE_P (ops[other_op])
	   && REGNO (ops[lreg]) == REGNO (ops[other_op]))
	  || rtx_equal_p (ops[other_op], ops[lreg]))
      return 0;
    }

  /* Do not accept frame_pointer_rtx as any operand.  */
  if (ops[lreg] == frame_pointer_rtx || ops[rreg] == frame_pointer_rtx
      || ops[rval] == frame_pointer_rtx
      || (other_op >= 0 && ops[other_op] == frame_pointer_rtx))
    return 0;

  if (code == PLUS
      && ! BASE_P (val_rtx))
    {

      /* Do not allow rx = rx + n if a normal add or sub with same size
	 would do.  */
      if (rtx_equal_p (ops[lreg], reg_rtx)
	  && GET_CODE (val_rtx) == CONST_INT
	  && (INTVAL (val_rtx) <= 63 && INTVAL (val_rtx) >= -63))
	return 0;

      /* Check allowed cases, like [r(+)?].[bwd] and const.
	 A symbol is not allowed with PIC.  */
      if (CONSTANT_P (val_rtx))
	return flag_pic == 0 || cris_symbol (val_rtx) == 0;

      if (GET_CODE (val_rtx) == MEM
	  && BASE_OR_AUTOINCR_P (XEXP (val_rtx, 0)))
	return 1;

      if (GET_CODE (val_rtx) == SIGN_EXTEND
	  && GET_CODE (XEXP (val_rtx, 0)) == MEM
	  && BASE_OR_AUTOINCR_P (XEXP (XEXP (val_rtx, 0), 0)))
	return 1;

      /* If we got here, it's not a valid addressing mode.  */
      return 0;
    }
  else if (code == MULT
	   || (code == PLUS && BASE_P (val_rtx)))
    {
      /* Do not allow rx = rx + ry.S, since it doesn't give better code.  */
      if (rtx_equal_p (ops[lreg], reg_rtx)
	  || (mult == 1 && rtx_equal_p (ops[lreg], val_rtx)))
	return 0;

      /* Do not allow bad multiply-values.  */
      if (mult != 1 && mult != 2 && mult != 4)
	return 0;

      /* Only allow  r + ...  */
      if (! BASE_P (reg_rtx))
	return 0;

      /* If we got here, all seems ok.
	 (All checks need to be done above).  */
      return 1;
    }

  /* If we get here, the caller got its initial tests wrong.  */
  internal_error ("internal error: cris_side_effect_mode_ok with bad operands");
}

/* The function reg_overlap_mentioned_p in CVS (still as of 2001-05-16)
   does not handle the case where the IN operand is strict_low_part; it
   does handle it for X.  Test-case in Axis-20010516.  This function takes
   care of that for THIS port.  FIXME: strict_low_part is going away
   anyway.  */

static int
cris_reg_overlap_mentioned_p (rtx x, rtx in)
{
  /* The function reg_overlap_mentioned now handles when X is
     strict_low_part, but not when IN is a STRICT_LOW_PART.  */
  if (GET_CODE (in) == STRICT_LOW_PART)
    in = XEXP (in, 0);

  return reg_overlap_mentioned_p (x, in);
}

/* The TARGET_ASM_NAMED_SECTION worker.
   We just dispatch to the functions for ELF and a.out.  */

void
cris_target_asm_named_section (const char *name, unsigned int flags,
			       tree decl)
{
  if (! TARGET_ELF)
    default_no_named_section (name, flags, decl);
  else
    default_elf_asm_named_section (name, flags, decl);
}

/* The LEGITIMATE_PIC_OPERAND_P worker.  */

int
cris_legitimate_pic_operand (rtx x)
{
  /* The PIC representation of a symbol with a GOT entry will be (for
     example; relocations differ):
      sym => [rPIC+sym:GOT]
     and for a GOT-less symbol it will be (for example, relocation differ):
      sym => rPIC+sym:GOTOFF
     so only a symbol with a GOT is by itself a valid operand, and it
     can't be a sum of a symbol and an offset.  */
  return ! cris_symbol (x) || cris_got_symbol (x);
}

/* Return nonzero if there's a SYMBOL_REF or LABEL_REF hiding inside this
   CONSTANT_P.  */

int
cris_symbol (rtx x)
{
  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
    case LABEL_REF:
      return 1;

    case UNSPEC:
      if (XINT (x, 1) == CRIS_UNSPEC_GOT || XINT (x, 1) != CRIS_UNSPEC_PLT)
	return 0;
      /* A PLT reference.  */
      ASSERT_PLT_UNSPEC (x);
      return 1;

    case CONST:
      return cris_symbol (XEXP (x, 0));

    case PLUS:
    case MINUS:
      return cris_symbol (XEXP (x, 0)) || cris_symbol (XEXP (x, 1));

    case CONST_INT:
    case CONST_DOUBLE:
      return 0;

    default:
      fatal_insn ("unrecognized supposed constant", x);
    }

  return 1;
}

/* Return nonzero if there's a SYMBOL_REF or LABEL_REF hiding inside this
   CONSTANT_P, and the symbol does not need a GOT entry.  Also set
   current_function_uses_pic_offset_table if we're generating PIC and ever
   see something that would need one.  */

int
cris_gotless_symbol (rtx x)
{
#ifdef ENABLE_CHECKING
  if (!flag_pic)
    abort ();
#endif

  switch (GET_CODE (x))
    {
    case UNSPEC:
      if (XINT (x, 1) == CRIS_UNSPEC_GOT)
	return 1;
      if (XINT (x, 1) != CRIS_UNSPEC_PLT)
	return 0;
      ASSERT_PLT_UNSPEC (x);
      return 1;

    case SYMBOL_REF:
      if (cfun != NULL)
	current_function_uses_pic_offset_table = 1;
      return SYMBOL_REF_LOCAL_P (x);

    case LABEL_REF:
      /* We don't set current_function_uses_pic_offset_table for
	 LABEL_REF:s in here, since they are almost always originating
	 from some branch.  The only time it does not come from a label is
	 when GCC does something like __builtin_setjmp.  Then we get the
	 LABEL_REF from the movsi expander, so we mark it there as a
	 special case.  */
      return 1;

    case CONST:
      return cris_gotless_symbol (XEXP (x, 0));

    case PLUS:
    case MINUS:
      {
	int x0 = cris_gotless_symbol (XEXP (x, 0)) != 0;
	int x1 = cris_gotless_symbol (XEXP (x, 1)) != 0;

	/* One and only one of them must be a local symbol.  Neither must
	   be some other, more general kind of symbol.  */
	return
	  (x0 ^ x1)
	  && ! (x0 == 0 && cris_symbol (XEXP (x, 0)))
	  && ! (x1 == 0 && cris_symbol (XEXP (x, 1)));
      }

    case CONST_INT:
    case CONST_DOUBLE:
      return 0;

    default:
      fatal_insn ("unrecognized supposed constant", x);
    }

  return 1;
}

/* Return nonzero if there's a SYMBOL_REF or LABEL_REF hiding inside this
   CONSTANT_P, and the symbol needs a GOT entry.  */

int
cris_got_symbol (rtx x)
{
#ifdef ENABLE_CHECKING
  if (!flag_pic)
    abort ();
#endif

  switch (GET_CODE (x))
    {
    case UNSPEC:
      if (XINT (x, 1) == CRIS_UNSPEC_GOT)
	return 0;
      ASSERT_PLT_UNSPEC (x);
      return 0;

    case SYMBOL_REF:
      if (cfun != NULL)
	current_function_uses_pic_offset_table = 1;
      return ! SYMBOL_REF_LOCAL_P (x);

    case CONST:
      return cris_got_symbol (XEXP (x, 0));

    case LABEL_REF:
      /* A LABEL_REF is never visible as a symbol outside the local
         function.  */
    case PLUS:
    case MINUS:
      /* Nope, can't access the GOT for "symbol + offset".  */
      return 0;

    case CONST_INT:
    case CONST_DOUBLE:
      return 0;

    default:
      fatal_insn ("unrecognized supposed constant in cris_global_pic_symbol",
		  x);
    }

  return 1;
}

/* TARGET_HANDLE_OPTION worker.  We just store the values into local
   variables here.  Checks for correct semantics are in
   cris_override_options.  */

static bool
cris_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_metrax100:
      target_flags
	|= (MASK_SVINTO
	    + MASK_ETRAX4_ADD
	    + MASK_ALIGN_BY_32);
      break;

    case OPT_mno_etrax100:
      target_flags
	&= ~(MASK_SVINTO
	     + MASK_ETRAX4_ADD
	     + MASK_ALIGN_BY_32);
      break;

    case OPT_m32_bit:
    case OPT_m32bit:
      target_flags
	|= (MASK_STACK_ALIGN
	    + MASK_CONST_ALIGN
	    + MASK_DATA_ALIGN
	    + MASK_ALIGN_BY_32);
      break;

    case OPT_m16_bit:
    case OPT_m16bit:
      target_flags
	|= (MASK_STACK_ALIGN
	    + MASK_CONST_ALIGN
	    + MASK_DATA_ALIGN);
      break;

    case OPT_m8_bit:
    case OPT_m8bit:
      target_flags
	&= ~(MASK_STACK_ALIGN
	     + MASK_CONST_ALIGN
	     + MASK_DATA_ALIGN);
      break;

    case OPT_max_stackframe_:
    case OPT_mmax_stackframe_:
      cris_max_stackframe_str = arg;
      break;

    case OPT_march_:
    case OPT_mcpu_:
      cris_cpu_str = arg;
      break;

    case OPT_mtune_:
      cris_tune_str = arg;
      break;

    default:
      break;
    }

  CRIS_SUBTARGET_HANDLE_OPTION(code, arg, value);

  return true;
}

/* The OVERRIDE_OPTIONS worker.
   As is the norm, this also parses -mfoo=bar type parameters.  */

void
cris_override_options (void)
{
  if (cris_max_stackframe_str)
    {
      cris_max_stackframe = atoi (cris_max_stackframe_str);

      /* Do some sanity checking.  */
      if (cris_max_stackframe < 0 || cris_max_stackframe > 0x20000000)
	internal_error ("-max-stackframe=%d is not usable, not between 0 and %d",
			cris_max_stackframe, 0x20000000);
    }

  /* Let "-metrax4" and "-metrax100" change the cpu version.  */
  if (TARGET_SVINTO && cris_cpu_version < CRIS_CPU_SVINTO)
    cris_cpu_version = CRIS_CPU_SVINTO;
  else if (TARGET_ETRAX4_ADD && cris_cpu_version < CRIS_CPU_ETRAX4)
    cris_cpu_version = CRIS_CPU_ETRAX4;

  /* Parse -march=... and its synonym, the deprecated -mcpu=...  */
  if (cris_cpu_str)
    {
      cris_cpu_version
	= (*cris_cpu_str == 'v' ? atoi (cris_cpu_str + 1) : -1);

      if (strcmp ("etrax4", cris_cpu_str) == 0)
	cris_cpu_version = 3;

      if (strcmp ("svinto", cris_cpu_str) == 0
	  || strcmp ("etrax100", cris_cpu_str) == 0)
	cris_cpu_version = 8;

      if (strcmp ("ng", cris_cpu_str) == 0
	  || strcmp ("etrax100lx", cris_cpu_str) == 0)
	cris_cpu_version = 10;

      if (cris_cpu_version < 0 || cris_cpu_version > 10)
	error ("unknown CRIS version specification in -march= or -mcpu= : %s",
	       cris_cpu_str);

      /* Set the target flags.  */
      if (cris_cpu_version >= CRIS_CPU_ETRAX4)
	target_flags |= MASK_ETRAX4_ADD;

      /* If this is Svinto or higher, align for 32 bit accesses.  */
      if (cris_cpu_version >= CRIS_CPU_SVINTO)
	target_flags
	  |= (MASK_SVINTO | MASK_ALIGN_BY_32
	      | MASK_STACK_ALIGN | MASK_CONST_ALIGN
	      | MASK_DATA_ALIGN);

      /* Note that we do not add new flags when it can be completely
	 described with a macro that uses -mcpu=X.  So
	 TARGET_HAS_MUL_INSNS is (cris_cpu_version >= CRIS_CPU_NG).  */
    }

  if (cris_tune_str)
    {
      int cris_tune
	= (*cris_tune_str == 'v' ? atoi (cris_tune_str + 1) : -1);

      if (strcmp ("etrax4", cris_tune_str) == 0)
	cris_tune = 3;

      if (strcmp ("svinto", cris_tune_str) == 0
	  || strcmp ("etrax100", cris_tune_str) == 0)
	cris_tune = 8;

      if (strcmp ("ng", cris_tune_str) == 0
	  || strcmp ("etrax100lx", cris_tune_str) == 0)
	cris_tune = 10;

      if (cris_tune < 0 || cris_tune > 10)
	error ("unknown CRIS cpu version specification in -mtune= : %s",
	       cris_tune_str);

      if (cris_tune >= CRIS_CPU_SVINTO)
	/* We have currently nothing more to tune than alignment for
	   memory accesses.  */
	target_flags
	  |= (MASK_STACK_ALIGN | MASK_CONST_ALIGN
	      | MASK_DATA_ALIGN | MASK_ALIGN_BY_32);
    }

  if (flag_pic)
    {
      /* Use error rather than warning, so invalid use is easily
	 detectable.  Still change to the values we expect, to avoid
	 further errors.  */
      if (! TARGET_LINUX)
	{
	  error ("-fPIC and -fpic are not supported in this configuration");
	  flag_pic = 0;
	}

      /* Turn off function CSE.  We need to have the addresses reach the
	 call expanders to get PLT-marked, as they could otherwise be
	 compared against zero directly or indirectly.  After visiting the
	 call expanders they will then be cse:ed, as the call expanders
	 force_reg the addresses, effectively forcing flag_no_function_cse
	 to 0.  */
      flag_no_function_cse = 1;
    }

  if (write_symbols == DWARF2_DEBUG && ! TARGET_ELF)
    {
      warning ("that particular -g option is invalid with -maout and -melinux");
      write_symbols = DBX_DEBUG;
    }

  /* Set the per-function-data initializer.  */
  init_machine_status = cris_init_machine_status;
}

/* The TARGET_ASM_OUTPUT_MI_THUNK worker.  */

static void
cris_asm_output_mi_thunk (FILE *stream,
			  tree thunkdecl ATTRIBUTE_UNUSED,
			  HOST_WIDE_INT delta,
			  HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
			  tree funcdecl)
{
  if (delta > 0)
    fprintf (stream, "\tadd%s " HOST_WIDE_INT_PRINT_DEC ",$%s\n",
	     ADDITIVE_SIZE_MODIFIER (delta), delta,
	     reg_names[CRIS_FIRST_ARG_REG]);
  else if (delta < 0)
    fprintf (stream, "\tsub%s " HOST_WIDE_INT_PRINT_DEC ",$%s\n",
	     ADDITIVE_SIZE_MODIFIER (-delta), -delta,
	     reg_names[CRIS_FIRST_ARG_REG]);

  if (flag_pic)
    {
      const char *name = XSTR (XEXP (DECL_RTL (funcdecl), 0), 0);

      name = (* targetm.strip_name_encoding) (name);
      fprintf (stream, "add.d ");
      assemble_name (stream, name);
      fprintf (stream, "%s,$pc\n", CRIS_PLT_PCOFFSET_SUFFIX);
    }
  else
    {
      fprintf (stream, "jump ");
      assemble_name (stream, XSTR (XEXP (DECL_RTL (funcdecl), 0), 0));
      fprintf (stream, "\n");
    }
}

/* Boilerplate emitted at start of file.  

   NO_APP *only at file start* means faster assembly.  It also means
   comments are not allowed.  In some cases comments will be output
   for debugging purposes.  Make sure they are allowed then.

   We want a .file directive only if TARGET_ELF.  */
static void
cris_file_start (void)
{
  /* These expressions can vary at run time, so we cannot put
     them into TARGET_INITIALIZER.  */
  targetm.file_start_app_off = !(TARGET_PDEBUG || flag_print_asm_name);
  targetm.file_start_file_directive = TARGET_ELF;

  default_file_start ();
}

/* Rename the function calls for integer multiply and divide.  */
static void
cris_init_libfuncs (void)
{
  set_optab_libfunc (smul_optab, SImode, "__Mul");
  set_optab_libfunc (sdiv_optab, SImode, "__Div");
  set_optab_libfunc (udiv_optab, SImode, "__Udiv");
  set_optab_libfunc (smod_optab, SImode, "__Mod");
  set_optab_libfunc (umod_optab, SImode, "__Umod");
}

/* The INIT_EXPANDERS worker sets the per-function-data initializer and
   mark functions.  */

void
cris_init_expanders (void)
{
  /* Nothing here at the moment.  */
}

/* Zero initialization is OK for all current fields.  */

static struct machine_function *
cris_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}

/* Split a 2 word move (DI or presumably DF) into component parts.
   Originally a copy of gen_split_move_double in m32r.c.  */

rtx
cris_split_movdx (rtx *operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);
  rtx dest = operands[0];
  rtx src  = operands[1];
  rtx val;

  /* We used to have to handle (SUBREG (MEM)) here, but that should no
     longer happen; after reload there are no SUBREGs any more, and we're
     only called after reload.  */
  if (GET_CODE (dest) == SUBREG || GET_CODE (src) == SUBREG)
    abort ();

  start_sequence ();
  if (GET_CODE (dest) == REG)
    {
      int dregno = REGNO (dest);

      /* Reg-to-reg copy.  */
      if (GET_CODE (src) == REG)
	{
	  int sregno = REGNO (src);

	  int reverse = (dregno == sregno + 1);

	  /* We normally copy the low-numbered register first.  However, if
	     the first register operand 0 is the same as the second register of
	     operand 1, we must copy in the opposite order.  */
	  emit_insn (gen_rtx_SET (VOIDmode,
				  operand_subword (dest, reverse, TRUE, mode),
				  operand_subword (src, reverse, TRUE, mode)));

	  emit_insn (gen_rtx_SET (VOIDmode,
				  operand_subword (dest, !reverse, TRUE, mode),
				  operand_subword (src, !reverse, TRUE, mode)));
	}
      /* Constant-to-reg copy.  */
      else if (GET_CODE (src) == CONST_INT || GET_CODE (src) == CONST_DOUBLE)
	{
	  rtx words[2];
	  split_double (src, &words[0], &words[1]);
	  emit_insn (gen_rtx_SET (VOIDmode,
				  operand_subword (dest, 0, TRUE, mode),
				  words[0]));

	  emit_insn (gen_rtx_SET (VOIDmode,
				  operand_subword (dest, 1, TRUE, mode),
				  words[1]));
	}
      /* Mem-to-reg copy.  */
      else if (GET_CODE (src) == MEM)
	{
	  /* If the high-address word is used in the address, we must load it
	     last.  Otherwise, load it first.  */
	  rtx addr = XEXP (src, 0);
	  int reverse
	    = (refers_to_regno_p (dregno, dregno + 1, addr, NULL) != 0);

	  /* The original code implies that we can't do
	     move.x [rN+],rM  move.x [rN],rM+1
	     when rN is dead, because of REG_NOTES damage.  That is
	     consistent with what I've seen, so don't try it.

             We have two different cases here; if the addr is POST_INC,
             just pass it through, otherwise add constants.  */

          if (GET_CODE (addr) == POST_INC)
	    {
	      emit_insn (gen_rtx_SET (VOIDmode,
				      operand_subword (dest, 0, TRUE, mode),
				      change_address (src, SImode, addr)));
	      emit_insn (gen_rtx_SET (VOIDmode,
				      operand_subword (dest, 1, TRUE, mode),
				      change_address (src, SImode, addr)));
	    }
	  else
	    {
	      /* Make sure we don't get any other addresses with
		 embedded postincrements.  They should be stopped in
		 GO_IF_LEGITIMATE_ADDRESS, but we're here for your
		 safety.  */
	      if (side_effects_p (addr))
		fatal_insn ("unexpected side-effects in address", addr);

	      emit_insn (gen_rtx_SET
			 (VOIDmode,
			  operand_subword (dest, reverse, TRUE, mode),
			  change_address
			  (src, SImode,
			   plus_constant (addr,
					  reverse * UNITS_PER_WORD))));
	      emit_insn (gen_rtx_SET
			 (VOIDmode,
			  operand_subword (dest, ! reverse, TRUE, mode),
			  change_address
			  (src, SImode,
			   plus_constant (addr,
					  (! reverse) *
					  UNITS_PER_WORD))));
	    }
	}
      else
	abort ();
    }
  /* Reg-to-mem copy or clear mem.  */
  else if (GET_CODE (dest) == MEM
	   && (GET_CODE (src) == REG
	       || src == const0_rtx
	       || src == CONST0_RTX (DFmode)))
    {
      rtx addr = XEXP (dest, 0);

      if (GET_CODE (addr) == POST_INC)
	{
	  emit_insn (gen_rtx_SET (VOIDmode,
				  change_address (dest, SImode, addr),
				  operand_subword (src, 0, TRUE, mode)));
	  emit_insn (gen_rtx_SET (VOIDmode,
				  change_address (dest, SImode, addr),
				  operand_subword (src, 1, TRUE, mode)));
	}
      else
	{
	  /* Make sure we don't get any other addresses with embedded
	     postincrements.  They should be stopped in
	     GO_IF_LEGITIMATE_ADDRESS, but we're here for your safety.  */
	  if (side_effects_p (addr))
	    fatal_insn ("unexpected side-effects in address", addr);

	  emit_insn (gen_rtx_SET
		     (VOIDmode,
		      change_address (dest, SImode, addr),
		      operand_subword (src, 0, TRUE, mode)));

	  emit_insn (gen_rtx_SET
		     (VOIDmode,
		      change_address (dest, SImode,
				      plus_constant (addr,
						     UNITS_PER_WORD)),
		      operand_subword (src, 1, TRUE, mode)));
	}
    }

  else
    abort ();

  val = get_insns ();
  end_sequence ();
  return val;
}

/* The expander for the prologue pattern name.  */

void
cris_expand_prologue (void)
{
  int regno;
  int size = get_frame_size ();
  /* Shorten the used name for readability.  */
  int cfoa_size = current_function_outgoing_args_size;
  int last_movem_reg = -1;
  int framesize = 0;
  rtx mem, insn;
  int return_address_on_stack = cris_return_address_on_stack ();
  int got_really_used = current_function_uses_pic_offset_table;
  int n_movem_regs = 0;
  int pretend = current_function_pretend_args_size;

  /* Don't do anything if no prologues or epilogues are wanted.  */
  if (!TARGET_PROLOGUE_EPILOGUE)
    return;

  if (size < 0)
    abort ();

  /* Align the size to what's best for the CPU model.  */
  if (TARGET_STACK_ALIGN)
    size = TARGET_ALIGN_BY_32 ? (size + 3) & ~3 : (size + 1) & ~1;

  if (pretend)
    {
      /* See also cris_setup_incoming_varargs where
	 cfun->machine->stdarg_regs is set.  There are other setters of
	 current_function_pretend_args_size than stdarg handling, like
	 for an argument passed with parts in R13 and stack.  We must
	 not store R13 into the pretend-area for that case, as GCC does
	 that itself.  "Our" store would be marked as redundant and GCC
	 will attempt to remove it, which will then be flagged as an
	 internal error; trying to remove a frame-related insn.  */
      int stdarg_regs = cfun->machine->stdarg_regs;

      framesize += pretend;

      for (regno = CRIS_FIRST_ARG_REG + CRIS_MAX_ARGS_IN_REGS - 1;
	   stdarg_regs > 0;
	   regno--, pretend -= 4, stdarg_regs--)
	{
	  insn = emit_insn (gen_rtx_SET (VOIDmode,
					 stack_pointer_rtx,
					 plus_constant (stack_pointer_rtx,
							-4)));
	  /* FIXME: When dwarf2 frame output and unless asynchronous
	     exceptions, make dwarf2 bundle together all stack
	     adjustments like it does for registers between stack
	     adjustments.  */
	  RTX_FRAME_RELATED_P (insn) = 1;

	  mem = gen_rtx_MEM (SImode, stack_pointer_rtx);
	  set_mem_alias_set (mem, get_varargs_alias_set ());
	  insn = emit_move_insn (mem, gen_rtx_raw_REG (SImode, regno));

	  /* Note the absence of RTX_FRAME_RELATED_P on the above insn:
	     the value isn't restored, so we don't want to tell dwarf2
	     that it's been stored to stack, else EH handling info would
	     get confused.  */
	}

      /* For other setters of current_function_pretend_args_size, we
	 just adjust the stack by leaving the remaining size in
	 "pretend", handled below.  */
    }

  /* Save SRP if not a leaf function.  */
  if (return_address_on_stack)
    {
      insn = emit_insn (gen_rtx_SET (VOIDmode,
				     stack_pointer_rtx,
				     plus_constant (stack_pointer_rtx,
						    -4 - pretend)));
      pretend = 0;
      RTX_FRAME_RELATED_P (insn) = 1;

      mem = gen_rtx_MEM (SImode, stack_pointer_rtx);
      set_mem_alias_set (mem, get_frame_alias_set ());
      insn = emit_move_insn (mem, gen_rtx_raw_REG (SImode, CRIS_SRP_REGNUM));
      RTX_FRAME_RELATED_P (insn) = 1;
      framesize += 4;
    }

  /* Set up the frame pointer, if needed.  */
  if (frame_pointer_needed)
    {
      insn = emit_insn (gen_rtx_SET (VOIDmode,
				     stack_pointer_rtx,
				     plus_constant (stack_pointer_rtx,
						    -4 - pretend)));
      pretend = 0;
      RTX_FRAME_RELATED_P (insn) = 1;

      mem = gen_rtx_MEM (SImode, stack_pointer_rtx);
      set_mem_alias_set (mem, get_frame_alias_set ());
      insn = emit_move_insn (mem, frame_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;

      insn = emit_move_insn (frame_pointer_rtx, stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;

      framesize += 4;
    }

  /* Between frame-pointer and saved registers lie the area for local
     variables.  If we get here with "pretended" size remaining, count
     it into the general stack size.  */
  size += pretend;

  /* Get a contiguous sequence of registers, starting with R0, that need
     to be saved.  */
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    {
      if (cris_reg_saved_in_regsave_area (regno, got_really_used))
	{
	  n_movem_regs++;

	  /* Check if movem may be used for registers so far.  */
	  if (regno == last_movem_reg + 1)
	    /* Yes, update next expected register.  */
	    last_movem_reg = regno;
	  else
	    {
	      /* We cannot use movem for all registers.  We have to flush
		 any movem:ed registers we got so far.  */
	      if (last_movem_reg != -1)
		{
		  int n_saved
		    = (n_movem_regs == 1) ? 1 : last_movem_reg + 1;

		  /* It is a win to use a side-effect assignment for
		     64 <= size <= 128.  But side-effect on movem was
		     not usable for CRIS v0..3.  Also only do it if
		     side-effects insns are allowed.  */
		  if ((last_movem_reg + 1) * 4 + size >= 64
		      && (last_movem_reg + 1) * 4 + size <= 128
		      && (cris_cpu_version >= CRIS_CPU_SVINTO || n_saved == 1)
		      && TARGET_SIDE_EFFECT_PREFIXES)
		    {
		      mem
			= gen_rtx_MEM (SImode,
				       plus_constant (stack_pointer_rtx,
						      -(n_saved * 4 + size)));
		      set_mem_alias_set (mem, get_frame_alias_set ());
		      insn
			= cris_emit_movem_store (mem, GEN_INT (n_saved),
						 -(n_saved * 4 + size),
						 true);
		    }
		  else
		    {
		      insn
			= gen_rtx_SET (VOIDmode,
				       stack_pointer_rtx,
				       plus_constant (stack_pointer_rtx,
						      -(n_saved * 4 + size)));
		      insn = emit_insn (insn);
		      RTX_FRAME_RELATED_P (insn) = 1;

		      mem = gen_rtx_MEM (SImode, stack_pointer_rtx);
		      set_mem_alias_set (mem, get_frame_alias_set ());
		      insn = cris_emit_movem_store (mem, GEN_INT (n_saved),
						    0, true);
		    }

		  framesize += n_saved * 4 + size;
		  last_movem_reg = -1;
		  size = 0;
		}

	      insn = emit_insn (gen_rtx_SET (VOIDmode,
					     stack_pointer_rtx,
					     plus_constant (stack_pointer_rtx,
							    -4 - size)));
	      RTX_FRAME_RELATED_P (insn) = 1;

	      mem = gen_rtx_MEM (SImode, stack_pointer_rtx);
	      set_mem_alias_set (mem, get_frame_alias_set ());
	      insn = emit_move_insn (mem, gen_rtx_raw_REG (SImode, regno));
	      RTX_FRAME_RELATED_P (insn) = 1;

	      framesize += 4 + size;
	      size = 0;
	    }
	}
    }

  /* Check after, if we could movem all registers.  This is the normal case.  */
  if (last_movem_reg != -1)
    {
      int n_saved
	= (n_movem_regs == 1) ? 1 : last_movem_reg + 1;

      /* Side-effect on movem was not usable for CRIS v0..3.  Also only
	 do it if side-effects insns are allowed.  */
      if ((last_movem_reg + 1) * 4 + size >= 64
	  && (last_movem_reg + 1) * 4 + size <= 128
	  && (cris_cpu_version >= CRIS_CPU_SVINTO || n_saved == 1)
	  && TARGET_SIDE_EFFECT_PREFIXES)
	{
	  mem
	    = gen_rtx_MEM (SImode,
			   plus_constant (stack_pointer_rtx,
					  -(n_saved * 4 + size)));
	  set_mem_alias_set (mem, get_frame_alias_set ());
	  insn = cris_emit_movem_store (mem, GEN_INT (n_saved),
					-(n_saved * 4 + size), true);
	}
      else
	{
	  insn
	    = gen_rtx_SET (VOIDmode,
			   stack_pointer_rtx,
			   plus_constant (stack_pointer_rtx,
					  -(n_saved * 4 + size)));
	  insn = emit_insn (insn);
	  RTX_FRAME_RELATED_P (insn) = 1;

	  mem = gen_rtx_MEM (SImode, stack_pointer_rtx);
	  set_mem_alias_set (mem, get_frame_alias_set ());
	  insn = cris_emit_movem_store (mem, GEN_INT (n_saved), 0, true);
	}

      framesize += n_saved * 4 + size;
      /* We have to put outgoing argument space after regs.  */
      if (cfoa_size)
	{
	  insn = emit_insn (gen_rtx_SET (VOIDmode,
					 stack_pointer_rtx,
					 plus_constant (stack_pointer_rtx,
							-cfoa_size)));
	  RTX_FRAME_RELATED_P (insn) = 1;
	  framesize += cfoa_size;
	}
    }
  else if ((size + cfoa_size) > 0)
    {
      insn = emit_insn (gen_rtx_SET (VOIDmode,
				     stack_pointer_rtx,
				     plus_constant (stack_pointer_rtx,
						    -(cfoa_size + size))));
      RTX_FRAME_RELATED_P (insn) = 1;
      framesize += size + cfoa_size;
    }

  /* Set up the PIC register, if it is used.  */
  if (got_really_used)
    {
      rtx got
	= gen_rtx_UNSPEC (SImode, gen_rtvec (1, const0_rtx), CRIS_UNSPEC_GOT);
      emit_move_insn (pic_offset_table_rtx, got);

      /* FIXME: This is a cover-up for flow2 messing up; it doesn't
	 follow exceptional paths and tries to delete the GOT load as
	 unused, if it isn't used on the non-exceptional paths.  Other
	 ports have similar or other cover-ups, or plain bugs marking
	 the GOT register load as maybe-dead.  To see this, remove the
	 line below and try libsupc++/vec.cc or a trivial
	 "static void y (); void x () {try {y ();} catch (...) {}}".  */
      emit_insn (gen_rtx_USE (VOIDmode, pic_offset_table_rtx));
    }

  if (cris_max_stackframe && framesize > cris_max_stackframe)
    warning ("stackframe too big: %d bytes", framesize);
}

/* The expander for the epilogue pattern.  */

void
cris_expand_epilogue (void)
{
  int regno;
  int size = get_frame_size ();
  int last_movem_reg = -1;
  int argspace_offset = current_function_outgoing_args_size;
  int pretend =	 current_function_pretend_args_size;
  rtx mem;
  bool return_address_on_stack = cris_return_address_on_stack ();
  /* A reference may have been optimized out
     (like the abort () in fde_split in unwind-dw2-fde.c, at least 3.2.1)
     so check that it's still used.  */
  int got_really_used = current_function_uses_pic_offset_table;
  int n_movem_regs = 0;

  if (!TARGET_PROLOGUE_EPILOGUE)
    return;

  /* Align byte count of stack frame.  */
  if (TARGET_STACK_ALIGN)
    size = TARGET_ALIGN_BY_32 ? (size + 3) & ~3 : (size + 1) & ~1;

  /* Check how many saved regs we can movem.  They start at r0 and must
     be contiguous.  */
  for (regno = 0;
       regno < FIRST_PSEUDO_REGISTER;
       regno++)
    if (cris_reg_saved_in_regsave_area (regno, got_really_used))
      {
	n_movem_regs++;

	if (regno == last_movem_reg + 1)
	  last_movem_reg = regno;
	else
	  break;
      }

  /* If there was only one register that really needed to be saved
     through movem, don't use movem.  */
  if (n_movem_regs == 1)
    last_movem_reg = -1;

  /* Now emit "normal" move insns for all regs higher than the movem
     regs.  */
  for (regno = FIRST_PSEUDO_REGISTER - 1;
       regno > last_movem_reg;
       regno--)
    if (cris_reg_saved_in_regsave_area (regno, got_really_used))
      {
	if (argspace_offset)
	  {
	    /* There is an area for outgoing parameters located before
	       the saved registers.  We have to adjust for that.  */
	    emit_insn (gen_rtx_SET (VOIDmode,
				    stack_pointer_rtx,
				    plus_constant (stack_pointer_rtx,
						   argspace_offset)));
	    /* Make sure we only do this once.  */
	    argspace_offset = 0;
	  }

	mem = gen_rtx_MEM (SImode, gen_rtx_POST_INC (SImode,
						     stack_pointer_rtx));
	set_mem_alias_set (mem, get_frame_alias_set ());
	emit_move_insn (gen_rtx_raw_REG (SImode, regno), mem);
      }

  /* If we have any movem-restore, do it now.  */
  if (last_movem_reg != -1)
    {
      if (argspace_offset)
	{
	  emit_insn (gen_rtx_SET (VOIDmode,
				  stack_pointer_rtx,
				  plus_constant (stack_pointer_rtx,
						 argspace_offset)));
	  argspace_offset = 0;
	}

      mem = gen_rtx_MEM (SImode,
			 gen_rtx_POST_INC (SImode, stack_pointer_rtx));
      set_mem_alias_set (mem, get_frame_alias_set ());
      emit_insn (cris_gen_movem_load (mem, GEN_INT (last_movem_reg + 1), 0));
    }

  /* If we don't clobber all of the allocated stack area (we've already
     deallocated saved registers), GCC might want to schedule loads from
     the stack to *after* the stack-pointer restore, which introduces an
     interrupt race condition.  This happened for the initial-value
     SRP-restore for g++.dg/eh/registers1.C (noticed by inspection of
     other failure for that test).  It also happened for the stack slot
     for the return value in (one version of)
     linux/fs/dcache.c:__d_lookup, at least with "-O2
     -fno-omit-frame-pointer".  */

  /* Restore frame pointer if necessary.  */
  if (frame_pointer_needed)
    {
      emit_insn (gen_cris_frame_deallocated_barrier ());

      emit_move_insn (stack_pointer_rtx, frame_pointer_rtx);
      mem = gen_rtx_MEM (SImode, gen_rtx_POST_INC (SImode,
						   stack_pointer_rtx));
      set_mem_alias_set (mem, get_frame_alias_set ());
      emit_move_insn (frame_pointer_rtx, mem);
    }
  else if ((size + argspace_offset) != 0)
    {
      emit_insn (gen_cris_frame_deallocated_barrier ());

      /* If there was no frame-pointer to restore sp from, we must
	 explicitly deallocate local variables.  */

      /* Handle space for outgoing parameters that hasn't been handled
	 yet.  */
      size += argspace_offset;

      emit_insn (gen_rtx_SET (VOIDmode,
			      stack_pointer_rtx,
			      plus_constant (stack_pointer_rtx, size)));
    }

  /* If this function has no pushed register parameters
     (stdargs/varargs), and if it is not a leaf function, then we have
     the return address on the stack.  */
  if (return_address_on_stack && pretend == 0)
    {
      if (current_function_calls_eh_return)
	{
	  rtx mem;
	  rtx srpreg = gen_rtx_raw_REG (SImode, CRIS_SRP_REGNUM);
	  mem = gen_rtx_MEM (SImode,
			     gen_rtx_POST_INC (SImode,
					       stack_pointer_rtx));
	  set_mem_alias_set (mem, get_frame_alias_set ());
	  emit_move_insn (srpreg, mem);

	  emit_insn (gen_addsi3 (stack_pointer_rtx,
				 stack_pointer_rtx,
				 gen_rtx_raw_REG (SImode,
						  CRIS_STACKADJ_REG)));
	  cris_expand_return (false);
	}
      else
	cris_expand_return (true);

      return;
    }

  /* If we pushed some register parameters, then adjust the stack for
     them.  */
  if (pretend != 0)
    {
      /* If SRP is stored on the way, we need to restore it first.  */
      if (return_address_on_stack)
	{
	  rtx mem;
	  rtx srpreg = gen_rtx_raw_REG (SImode, CRIS_SRP_REGNUM);
	  mem = gen_rtx_MEM (SImode,
			     gen_rtx_POST_INC (SImode,
					       stack_pointer_rtx));
	  set_mem_alias_set (mem, get_frame_alias_set ());
	  emit_move_insn (srpreg, mem);
	}

      emit_insn (gen_rtx_SET (VOIDmode,
			      stack_pointer_rtx,
			      plus_constant (stack_pointer_rtx, pretend)));
    }

  /* Perform the "physical" unwinding that the EH machinery calculated.  */
  if (current_function_calls_eh_return)
    emit_insn (gen_addsi3 (stack_pointer_rtx,
			   stack_pointer_rtx,
			   gen_rtx_raw_REG (SImode,
					    CRIS_STACKADJ_REG)));
  cris_expand_return (false);
}

/* Worker function for generating movem from mem for load_multiple.  */

rtx
cris_gen_movem_load (rtx src, rtx nregs_rtx, int nprefix)
{
  int nregs = INTVAL (nregs_rtx);
  rtvec vec;
  int eltno = 1;
  int i;
  rtx srcreg = XEXP (src, 0);
  unsigned int regno = nregs - 1;
  int regno_inc = -1;

  if (GET_CODE (srcreg) == POST_INC)
    srcreg = XEXP (srcreg, 0);

  if (!REG_P (srcreg))
    abort ();

  /* Don't use movem for just one insn.  The insns are equivalent except
     for the pipeline hazard (on v32); movem does not forward the loaded
     registers so there's a three cycles penalty for their use.  */
  if (nregs == 1)
    return gen_movsi (gen_rtx_REG (SImode, 0), src);

  vec = rtvec_alloc (nprefix + nregs
		     + (GET_CODE (XEXP (src, 0)) == POST_INC));

  if (GET_CODE (XEXP (src, 0)) == POST_INC)
    {
      RTVEC_ELT (vec, nprefix + 1)
	= gen_rtx_SET (VOIDmode, srcreg, plus_constant (srcreg, nregs * 4));
      eltno++;
    }

  src = replace_equiv_address (src, srcreg);
  RTVEC_ELT (vec, nprefix)
    = gen_rtx_SET (VOIDmode, gen_rtx_REG (SImode, regno), src);
  regno += regno_inc;

  for (i = 1; i < nregs; i++, eltno++)
    {
      RTVEC_ELT (vec, nprefix + eltno)
	= gen_rtx_SET (VOIDmode, gen_rtx_REG (SImode, regno),
		       adjust_address_nv (src, SImode, i * 4));
      regno += regno_inc;
    }

  return gen_rtx_PARALLEL (VOIDmode, vec);
}

/* Worker function for generating movem to mem.  If FRAME_RELATED, notes
   are added that the dwarf2 machinery understands.  */

rtx
cris_emit_movem_store (rtx dest, rtx nregs_rtx, int increment,
		       bool frame_related)
{
  int nregs = INTVAL (nregs_rtx);
  rtvec vec;
  int eltno = 1;
  int i;
  rtx insn;
  rtx destreg = XEXP (dest, 0);
  unsigned int regno = nregs - 1;
  int regno_inc = -1;

  if (GET_CODE (destreg) == POST_INC)
    increment += nregs * 4;

  if (GET_CODE (destreg) == POST_INC || GET_CODE (destreg) == PLUS)
    destreg = XEXP (destreg, 0);

  if (!REG_P (destreg))
    abort ();

  /* Don't use movem for just one insn.  The insns are equivalent except
     for the pipeline hazard (on v32); movem does not forward the loaded
     registers so there's a three cycles penalty for use.  */
  if (nregs == 1)
    {
      rtx mov = gen_rtx_SET (VOIDmode, dest, gen_rtx_REG (SImode, 0));

      if (increment == 0)
	{
	  insn = emit_insn (mov);
	  if (frame_related)
	    RTX_FRAME_RELATED_P (insn) = 1;
	  return insn;
	}

      /* If there was a request for a side-effect, create the ordinary
         parallel.  */
      vec = rtvec_alloc (2);

      RTVEC_ELT (vec, 0) = mov;
      RTVEC_ELT (vec, 1) = gen_rtx_SET (VOIDmode, destreg,
					plus_constant (destreg, increment));
      if (frame_related)
	{
	  RTX_FRAME_RELATED_P (mov) = 1;
	  RTX_FRAME_RELATED_P (RTVEC_ELT (vec, 1)) = 1;
	}
    }
  else
    {
      vec = rtvec_alloc (nregs + (increment != 0 ? 1 : 0));
      RTVEC_ELT (vec, 0)
	= gen_rtx_SET (VOIDmode,
		       replace_equiv_address (dest,
					      plus_constant (destreg,
							     increment)),
		       gen_rtx_REG (SImode, regno));
      regno += regno_inc;

      /* The dwarf2 info wants this mark on each component in a parallel
	 that's part of the prologue (though it's optional on the first
	 component).  */
      if (frame_related)
	RTX_FRAME_RELATED_P (RTVEC_ELT (vec, 0)) = 1;

      if (increment != 0)
	{
	  RTVEC_ELT (vec, 1)
	    = gen_rtx_SET (VOIDmode, destreg,
			   plus_constant (destreg,
					  increment != 0
					  ? increment : nregs * 4));
	  eltno++;

	  if (frame_related)
	    RTX_FRAME_RELATED_P (RTVEC_ELT (vec, 1)) = 1;

	  /* Don't call adjust_address_nv on a post-incremented address if
	     we can help it.  */
	  if (GET_CODE (XEXP (dest, 0)) == POST_INC)
	    dest = replace_equiv_address (dest, destreg);
	}

      for (i = 1; i < nregs; i++, eltno++)
	{
	  RTVEC_ELT (vec, eltno)
	    = gen_rtx_SET (VOIDmode, adjust_address_nv (dest, SImode, i * 4),
			   gen_rtx_REG (SImode, regno));
	  if (frame_related)
	    RTX_FRAME_RELATED_P (RTVEC_ELT (vec, eltno)) = 1;
	  regno += regno_inc;
	}
    }

  insn = emit_insn (gen_rtx_PARALLEL (VOIDmode, vec));

  /* Because dwarf2out.c handles the insns in a parallel as a sequence,
     we need to keep the stack adjustment separate, after the
     MEM-setters.  Else the stack-adjustment in the second component of
     the parallel would be mishandled; the offsets for the SETs that
     follow it would be wrong.  We prepare for this by adding a
     REG_FRAME_RELATED_EXPR with the MEM-setting parts in a SEQUENCE
     followed by the increment.  Note that we have FRAME_RELATED_P on
     all the SETs, including the original stack adjustment SET in the
     parallel.  */
  if (frame_related)
    {
      if (increment != 0)
	{
	  rtx seq = gen_rtx_SEQUENCE (VOIDmode, rtvec_alloc (nregs + 1));
	  XVECEXP (seq, 0, 0) = XVECEXP (PATTERN (insn), 0, 0);
	  for (i = 1; i < nregs; i++)
	    XVECEXP (seq, 0, i) = XVECEXP (PATTERN (insn), 0, i + 1);
	  XVECEXP (seq, 0, nregs) = XVECEXP (PATTERN (insn), 0, 1);
	  REG_NOTES (insn)
	    = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR, seq,
				 REG_NOTES (insn));
	}

      RTX_FRAME_RELATED_P (insn) = 1;
    }

  return insn;
}

/* Use from within code, from e.g. PRINT_OPERAND and
   PRINT_OPERAND_ADDRESS.  Macros used in output_addr_const need to emit
   different things depending on whether code operand or constant is
   emitted.  */

static void
cris_output_addr_const (FILE *file, rtx x)
{
  in_code++;
  output_addr_const (file, x);
  in_code--;
}

/* Worker function for ASM_OUTPUT_SYMBOL_REF.  */

void
cris_asm_output_symbol_ref (FILE *file, rtx x)
{
  if (flag_pic && in_code > 0)
    {
      const char *origstr = XSTR (x, 0);
      const char *str;

      str = (* targetm.strip_name_encoding) (origstr);

      if (cris_gotless_symbol (x))
	{
	  if (! cris_pic_sympart_only)
	    fprintf (file, "$%s+", reg_names [PIC_OFFSET_TABLE_REGNUM]);
	  assemble_name (file, str);
	  fprintf (file, ":GOTOFF");
	}
      else if (cris_got_symbol (x))
	{
	  if (cris_pic_sympart_only)
	    abort ();
	  fprintf (file, "[$%s+", reg_names [PIC_OFFSET_TABLE_REGNUM]);
	  assemble_name (file, XSTR (x, 0));

	  if (flag_pic == 1)
	    fprintf (file, ":GOT16]");
	  else
	    fprintf (file, ":GOT]");
	}
      else
	LOSE_AND_RETURN ("unexpected PIC symbol", x);

      /* Sanity check.  */
      if (! current_function_uses_pic_offset_table)
	output_operand_lossage ("PIC register isn't set up");
    }
  else
    assemble_name (file, XSTR (x, 0));
}

/* Worker function for ASM_OUTPUT_LABEL_REF.  */

void
cris_asm_output_label_ref (FILE *file, char *buf)
{
  if (flag_pic && in_code > 0)
    {
      if (! cris_pic_sympart_only)
	fprintf (file, "$%s+", reg_names [PIC_OFFSET_TABLE_REGNUM]);
      assemble_name (file, buf);

      fprintf (file, ":GOTOFF");

      /* Sanity check.  */
      if (! current_function_uses_pic_offset_table)
	internal_error ("emitting PIC operand, but PIC register isn't set up");
    }
  else
    assemble_name (file, buf);
}

/* Worker function for OUTPUT_ADDR_CONST_EXTRA.  */

bool
cris_output_addr_const_extra (FILE *file, rtx x)
{
  switch (GET_CODE (x))
    {
      const char *origstr;
      const char *str;

    case UNSPEC:
      ASSERT_PLT_UNSPEC (x);
      x = XVECEXP (x, 0, 0);
      origstr = XSTR (x, 0);
      str = (* targetm.strip_name_encoding) (origstr);
      if (cris_pic_sympart_only)
	{
	  assemble_name (file, str);
	  fprintf (file, ":PLTG");
	}
      else
	{
	  if (TARGET_AVOID_GOTPLT)
	    /* We shouldn't get here.  */
	    abort ();

	  fprintf (file, "[$%s+", reg_names [PIC_OFFSET_TABLE_REGNUM]);
	  assemble_name (file, XSTR (x, 0));

	  if (flag_pic == 1)
	    fprintf (file, ":GOTPLT16]");
	  else
	    fprintf (file, ":GOTPLT]");
	}
      return true;

    default:
      return false;
    }
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
cris_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		       int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, CRIS_STRUCT_VALUE_REGNUM);
}

/* Worker function for TARGET_SETUP_INCOMING_VARARGS.  */

static void
cris_setup_incoming_varargs (CUMULATIVE_ARGS *ca,
			     enum machine_mode mode ATTRIBUTE_UNUSED,
			     tree type ATTRIBUTE_UNUSED,
			     int *pretend_arg_size,
			     int second_time)
{
  if (ca->regs < CRIS_MAX_ARGS_IN_REGS)
    {
      int stdarg_regs = CRIS_MAX_ARGS_IN_REGS - ca->regs;
      cfun->machine->stdarg_regs = stdarg_regs;
      *pretend_arg_size = stdarg_regs * 4;
    }

  if (TARGET_PDEBUG)
    fprintf (asm_out_file,
	     "\n; VA:: ANSI: %d args before, anon @ #%d, %dtime\n",
	     ca->regs, *pretend_arg_size, second_time);
}

/* Return true if TYPE must be passed by invisible reference.
   For cris, we pass <= 8 bytes by value, others by reference.  */

static bool
cris_pass_by_reference (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED,
			enum machine_mode mode, tree type,
			bool named ATTRIBUTE_UNUSED)
{
  return (targetm.calls.must_pass_in_stack (mode, type)
	  || CRIS_FUNCTION_ARG_SIZE (mode, type) > 8);
}


static int
cris_arg_partial_bytes (CUMULATIVE_ARGS *ca, enum machine_mode mode,
			tree type, bool named ATTRIBUTE_UNUSED)
{
  if (ca->regs == CRIS_MAX_ARGS_IN_REGS - 1
      && !targetm.calls.must_pass_in_stack (mode, type)
      && CRIS_FUNCTION_ARG_SIZE (mode, type) > 4
      && CRIS_FUNCTION_ARG_SIZE (mode, type) <= 8)
    return UNITS_PER_WORD;
  else
    return 0;
}

/* Worker function for TARGET_MD_ASM_CLOBBERS.  */

static tree
cris_md_asm_clobbers (tree outputs, tree inputs, tree clobbers)
{
  HARD_REG_SET mof_set;
  tree t;

  CLEAR_HARD_REG_SET (mof_set);
  SET_HARD_REG_BIT (mof_set, CRIS_MOF_REGNUM);

  for (t = outputs; t != NULL; t = TREE_CHAIN (t))
    {
      tree val = TREE_VALUE (t);

      /* The constraint letter for the singleton register class of MOF
	 is 'h'.  If it's mentioned in the constraints, the asm is
	 MOF-aware and adding it to the clobbers would cause it to have
	 impossible constraints.  */
      if (strchr (TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (t))),
		  'h') != NULL
	  || decl_overlaps_hard_reg_set_p (val, mof_set))
	return clobbers;
    }

  for (t = inputs; t != NULL; t = TREE_CHAIN (t))
    {
      tree val = TREE_VALUE (t);

      if (strchr (TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (t))),
		  'h') != NULL
	  || decl_overlaps_hard_reg_set_p (val, mof_set))
	return clobbers;
    }

  return tree_cons (NULL_TREE,
		    build_string (strlen (reg_names[CRIS_MOF_REGNUM]),
				  reg_names[CRIS_MOF_REGNUM]),
		    clobbers);
}

#if 0
/* Various small functions to replace macros.  Only called from a
   debugger.  They might collide with gcc functions or system functions,
   so only emit them when '#if 1' above.  */

enum rtx_code Get_code (rtx);

enum rtx_code
Get_code (rtx x)
{
  return GET_CODE (x);
}

const char *Get_mode (rtx);

const char *
Get_mode (rtx x)
{
  return GET_MODE_NAME (GET_MODE (x));
}

rtx Xexp (rtx, int);

rtx
Xexp (rtx x, int n)
{
  return XEXP (x, n);
}

rtx Xvecexp (rtx, int, int);

rtx
Xvecexp (rtx x, int n, int m)
{
  return XVECEXP (x, n, m);
}

int Get_rtx_len (rtx);

int
Get_rtx_len (rtx x)
{
  return GET_RTX_LENGTH (GET_CODE (x));
}

/* Use upper-case to distinguish from local variables that are sometimes
   called next_insn and prev_insn.  */

rtx Next_insn (rtx);

rtx
Next_insn (rtx insn)
{
  return NEXT_INSN (insn);
}

rtx Prev_insn (rtx);

rtx
Prev_insn (rtx insn)
{
  return PREV_INSN (insn);
}
#endif

#include "gt-cris.h"

/*
 * Local variables:
 * eval: (c-set-style "gnu")
 * indent-tabs-mode: t
 * End:
 */
