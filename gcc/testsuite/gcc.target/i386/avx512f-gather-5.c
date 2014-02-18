/* { dg-do compile } */
/* { dg-options "-O3 -mavx512f" } */

#include "avx512f-gather-4.c"

/* { dg-final { scan-assembler "gather\[^\n\]*zmm" } } */
/* { dg-final { scan-assembler-not "gather\[^\n\]*ymm\[^\n\]*ymm" } } */
/* { dg-final { scan-assembler-not "gather\[^\n\]*xmm\[^\n\]*ymm" } } */
/* { dg-final { scan-assembler-not "gather\[^\n\]*ymm\[^\n\]*xmm" } } */
/* { dg-final { scan-assembler-not "gather\[^\n\]*xmm\[^\n\]*xmm" } } */
