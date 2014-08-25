/* { dg-do link } */
/* { dg-options "-O1 -fdelete-null-pointer-checks -fdump-tree-ccp1" } */

void foo();

int main()
{
  if (foo == (void *)0)
    link_error ();
  return 0;
}

/* { dg-final { scan-tree-dump-not "if \\(foo" "ccp1" { target { ! avr*-*-* } } } } */
/* { dg-final { cleanup-tree-dump "ccp1" } } */
