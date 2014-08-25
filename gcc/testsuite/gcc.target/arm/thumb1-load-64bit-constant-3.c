/* { dg-require-effective-target arm_thumb1_ok } */
/* { dg-do compile } */
/* { dg-options "-Os" } */
/* { dg-skip-if "" { ! { arm_thumb1 } } } */

long long
foo (int len)
{
  return (long long) (((long long) 1 << len) - 1);
}

/* { dg-final { scan-assembler-not "ldr" } } */
/* { dg-final { scan-assembler-times "neg" 1 } } */

