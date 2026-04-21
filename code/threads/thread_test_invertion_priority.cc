#include "thread_test_invertion_priority.hh"
#include "lock.hh"
#include "system.hh"
#include "thread.hh"
#include "lib/utility.hh"
#include <stdio.h>

static Lock *synchLock; // Protege la variable value


void
functionA(void*)
{
  DEBUG('q', "Ejecutando %s\n", currentThread->GetName());

  synchLock->Acquire();
  DEBUG('q', "El thread %s tomó el lock\n", currentThread->GetName());
  currentThread->Yield();
  synchLock->Release();
  DEBUG('q', "El thread %s soltó el lock\n", currentThread->GetName());
}

void
functionC(void*)
{
  DEBUG('q', "Ejecutando %s\n", currentThread->GetName());

  synchLock->Acquire();
  DEBUG('q', "El thread %s tomó el lock\n", currentThread->GetName());
  synchLock->Release();
  DEBUG('q', "El thread %s soltó el lock\n", currentThread->GetName());
}

void ThreadTestInvertionPriority() 
{
  printf("=== Invertion Priority Test ===\n");

  synchLock = new Lock("Lock");

  Thread *a = new Thread("A", true, 5);
  Thread *c = new Thread("C", true, 9);

  a->Fork(functionA, NULL);
  
  currentThread->Yield();
  
  c->Fork(functionC, NULL);
  
  a->Join();
  c->Join();

  delete synchLock;
}