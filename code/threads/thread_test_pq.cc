#include "thread_test_pq.hh"
#include "thread.hh"
#include "system.hh"

#include <stdio.h>

static void
PrintName(void* arg){
    printf("%s\n", currentThread->GetName());
}


void
ThreadTestPriorityQueue()
{
    printf("=== Priority Queues Test ===\n");

    Thread *a = new Thread("A", true, 6);
    Thread *b = new Thread("B", true, 9);
    Thread *c = new Thread("C", true, 7);
    Thread *d = new Thread("D", true, 8);

    a->Fork(PrintName, NULL);
    b->Fork(PrintName, NULL);
    c->Fork(PrintName, NULL);
    d->Fork(PrintName, NULL);

    printf("a: %p\n", a);
    printf("b: %p\n", b);
    printf("c: %p\n", c);
    printf("d: %p\n", d);

    b->Join();
    d->Join();
    c->Join();
    a->Join();
}
