/* { dg-do compile } */
/* { dg-options "-O2 -ftree-vectorize -msse2 -mno-sse3 -fdump-tree-vect-details" } */


void test (short* a, short* b)
{
  int i;
  for (i = 0; i < 10000; ++i)
    a[i] = abs (b[i]);
}


/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 1 "vect" } } */
/* { dg-final { cleanup-tree-dump "vect" } } */
