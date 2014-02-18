/* { dg-shouldfail "tsan" } */

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

int Global;

void __attribute__((noinline)) foo1() {
  Global = 42;
}

void __attribute__((noinline)) bar1() {
  volatile int tmp = 42; (void)tmp;
  foo1();
}

void __attribute__((noinline)) foo2() {
  volatile int v = Global; (void)v;
}

void __attribute__((noinline)) bar2() {
  volatile int tmp = 42; (void)tmp;
  foo2();
}

void *Thread1(void *x) {
  sleep(1);
  bar1();
  return NULL;
}

void *Thread2(void *x) {
  bar2();
  return NULL;
}

void StartThread(pthread_t *t, void *(*f)(void*)) {
  pthread_create(t, NULL, f, NULL);
}

int main() {
  pthread_t t[2];
  StartThread(&t[0], Thread1);
  StartThread(&t[1], Thread2);
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);
  return 0;
}

/* { dg-output "WARNING: ThreadSanitizer: data race.*" } */
/* { dg-output "  Write of size 4 at .* by thread T1:(\n|\r\n|\r)" } */
/* { dg-output "    #0 foo1.* .*(simple_stack.c:10|\\?{2}:0) (.*)" } */
/* { dg-output "    #1 bar1.* .*(simple_stack.c:15|\\?{2}:0) (.*)" } */
/* { dg-output "    #2 Thread1.* .*(simple_stack.c:29|\\?{2}:0) (.*)" } */
/* { dg-output "  Previous read of size 4 at .* by thread T2:(\n|\r\n|\r)" } */
/* { dg-output "    #0 foo2.* .*(simple_stack.c:19|\\?{2}:0) (.*)" } */
/* { dg-output "    #1 bar2.* .*(simple_stack.c:24|\\?{2}:0) (.*)" } */
/* { dg-output "    #2 Thread2.* .*(simple_stack.c:34|\\?{2}:0) (.*)" } */
/* { dg-output "  Thread T1 \\(tid=.*, running\\) created by main thread at:(\n|\r\n|\r)" } */
/* { dg-output "    #0 pthread_create .* (.*)" } */
/* { dg-output "    #1 StartThread.* .*(simple_stack.c:39|\\?{2}:0) (.*)" } */
/* { dg-output "  Thread T2 (.*) created by main thread at:(\n|\r\n|\r)" } */
/* { dg-output "    #0 pthread_create .* (.*)" } */
/* { dg-output "    #1 StartThread.* .*(simple_stack.c:39|\\?{2}:0) (.*)" } */
