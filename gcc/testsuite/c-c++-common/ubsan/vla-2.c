/* { dg-do run } */
/* { dg-options "-fsanitize=vla-bound -Wall -Wno-unused-variable" } */

int
main (void)
{
  const int t = 0;
  struct s {
    int x;
    /* Don't instrument this one.  */
    int g[t];
  };

  return 0;
}
