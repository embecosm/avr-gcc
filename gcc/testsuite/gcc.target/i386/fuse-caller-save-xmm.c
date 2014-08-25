/* { dg-do compile } */
/* { dg-options "-O2 -msse2 -mno-avx -fuse-caller-save -fomit-frame-pointer" } */

typedef double v2df __attribute__((vector_size (16)));

static v2df __attribute__((noinline))
bar (v2df a)
{
  return a + (v2df){ 3.0, 3.0 };
}

v2df __attribute__((noinline))
foo (v2df y)
{
  return y + bar (y);
}

/* Check presence of all insns on xmm registers.  These checks are expected to
   pass with both -fuse-caller-save and -fno-use-caller-save.  */
/* { dg-final { scan-assembler-times "addpd\t\\.?LC0.*, %xmm0" 1 } } */
/* { dg-final { scan-assembler-times "addpd\t%xmm1, %xmm0" 1 } } */
/* { dg-final { scan-assembler-times "movapd\t%xmm0, %xmm1" 1 } } */

/* Check absence of save/restore of xmm1 register.  */
/* { dg-final { scan-assembler-not "movaps\t%xmm1, \\(%\[re\]?sp\\)" } } */
/* { dg-final { scan-assembler-not "movapd\t\\(%\[re\]?sp\\), %xmm1" } } */
