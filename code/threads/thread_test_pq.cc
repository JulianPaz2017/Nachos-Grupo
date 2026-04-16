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
    printf("===` Priority Queues Test ===\n");

    Thread *a = new Thread("A", true, 0);
    Thread *b = new Thread("B", true, 9);
    Thread *c = new Thread("C", true, 3);
    Thread *d = new Thread("D", true, 8);

    a->Fork(PrintName, NULL);
    b->Fork(PrintName, NULL);
    c->Fork(PrintName, NULL);
    d->Fork(PrintName, NULL);

    a->Join();
    b->Join();
    c->Join();
    d->Join();
}
