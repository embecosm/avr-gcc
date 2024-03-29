/* { dg-do compile } */
/* { dg-options "-O1 -fdump-tree-optimized" } */
static struct a {int magic1,b;} a;
volatile int magic2;
static struct b {int a,b,c,d,e,f;} magic3;

struct b foo();

t()
{
 a.magic1 = 1;
 magic2 = 1;
 magic3 = foo();
}
/* { dg-final { scan-tree-dump-not "magic1" "optimized"} } */
/* { dg-final { scan-tree-dump-not "magic3" "optimized"} } */
/* { dg-final { scan-tree-dump "magic2" "optimized"} } */
/* { dg-final { scan-tree-dump "foo" "optimized"} } */
 
/* { dg-final { cleanup-tree-dump "optimized" } } */
