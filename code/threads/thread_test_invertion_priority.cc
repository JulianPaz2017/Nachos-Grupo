#include "thread_test_invertion_priority.hh"
#include "lock.hh"
#include "system.hh"
#include "thread.hh"
#include "lib/utility.hh"
#include <stdio.h>

bool cFinished;
static Lock *mutex;


void
functionA(void * arg)
{
  DEBUG('q', "Ejecutando %s\n", currentThread->GetName());

  mutex->Acquire();
  DEBUG('q', "El thread %s tomó el lock\n", currentThread->GetName());
  currentThread->Yield();
  mutex->Release();
  DEBUG('q', "El thread %s soltó el lock\n", currentThread->GetName());
}

void
functionC(void * arg)
{
  DEBUG('q', "Ejecutando %s\n", currentThread->GetName());

  mutex->Acquire();
  DEBUG('q', "El thread %s tomó el lock\n", currentThread->GetName());
  mutex->Release();
  DEBUG('q', "El thread %s soltó el lock\n", currentThread->GetName());
}

void ThreadTestInvertionPriority() {
  printf("=== Invertion Priority Test ===\n");

  mutex = new Lock("Lock");
  cFinished = false;

  Thread *a = new Thread("A", true, 5);
  Thread *c = new Thread("C", true, 9);

  a->Fork(functionA, NULL);

  currentThread->Yield();

  c->Fork(functionC, NULL);

  a->Join();
  c->Join();

  delete mutex;
  return;
}