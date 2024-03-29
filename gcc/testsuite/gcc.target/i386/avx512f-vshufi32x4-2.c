/* { dg-do run } */
/* { dg-options "-O2 -mavx512f" } */
/* { dg-require-effective-target avx512f } */

#define AVX512F

#include "avx512f-helper.h"

#define SIZE (AVX512F_LEN / 32)
#include "avx512f-mask-type.h"
#include "string.h"

static void
CALC (int *e, UNION_TYPE (AVX512F_LEN, i_d) s1,
      UNION_TYPE (AVX512F_LEN, i_d) s2, int imm)
{
  int i, offset, selector;
  int *source;
  for (i = 0; i < SIZE / 4; i++)
    {

#if AVX512F_LEN == 512
      selector = (imm >> i * 2) & 0x3;
#else
      selector = (imm >> i) & 0x1;
#endif

      offset = i * 4;
      source = i * 4 * 32 < AVX512F_LEN / 2 ? s1.a : s2.a;
      memcpy (e + offset, source + selector * 4, 16);
    }
}

void
TEST (void)
{
  UNION_TYPE (AVX512F_LEN, i_d) u1, u2, u3, s1, s2;
  MASK_TYPE mask = MASK_VALUE;
  int e[SIZE];
  int i;
  int imm = 203;

  for (i = 0; i < SIZE; i++)
    {
      s1.a[i] = 1.2 / (i + 0.378);
      s2.a[i] = 91.02 / (i + 4.3578);
      u1.a[i] = DEFAULT_VALUE;
      u2.a[i] = DEFAULT_VALUE;
      u3.a[i] = DEFAULT_VALUE;
    }

  u1.x = INTRINSIC (_shuffle_i32x4) (s1.x, s2.x, imm);
  u2.x = INTRINSIC (_mask_shuffle_i32x4) (u2.x, mask, s1.x, s2.x, imm);
  u3.x = INTRINSIC (_maskz_shuffle_i32x4) (mask, s1.x, s2.x, imm);

  CALC (e, s1, s2, imm);

  if (UNION_CHECK (AVX512F_LEN, i_d) (u1, e))
    abort ();

  MASK_MERGE (i_d) (e, mask, SIZE);
  if (UNION_CHECK (AVX512F_LEN, i_d) (u2, e))
    abort ();

  MASK_ZERO (i_d) (e, mask, SIZE);
  if (UNION_CHECK (AVX512F_LEN, i_d) (u3, e))
    abort ();
}
