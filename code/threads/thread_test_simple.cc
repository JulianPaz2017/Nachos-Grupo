/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2007-2009 Universidad de Las Palmas de Gran Canaria.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "thread_test_simple.hh"
#include "system.hh"


#include <stdio.h>
#include <string.h>


#ifdef SEMAPHORE_TEST
#include "semaphore.hh"


static Semaphore *s = nullptr;
#endif


/// Flags para saber cuándo terminó cada hilo secundario.
static bool thread2Done = false;
static bool thread3Done = false;
static bool thread4Done = false;
static bool thread5Done = false;


/// Loop 10 times, yielding the CPU to another ready thread each iteration.
///
/// * `name_` points to a string with a thread name, just for debugging
///   purposes.
void
SimpleThread(void *name_)
{
   #ifdef SEMAPHORE_TEST
       s->P(); 
       DEBUG('s', "Thread `%s` doing P() on semaphore `%s`\n", currentThread->GetName(), s->GetName());
   #endif


   for (unsigned num = 0; num < 10; num++) {
       printf("*** Thread `%s` is running: iteration %u\n", currentThread->GetName(), num);
       currentThread->Yield();
   }


   printf("!!! Thread `%s` has finished SimpleThread\n", currentThread->GetName());


   #ifdef SEMAPHORE_TEST
       s->V();
       DEBUG('s', "Thread `%s` doing V() on semaphore `%s`\n", currentThread->GetName(), s->GetName());
   #endif


   // Marcar el hilo como terminado
   if (strcmp(currentThread->GetName(), "2nd") == 0) {
       thread2Done = true;
   } else if (strcmp(currentThread->GetName(), "3rd") == 0) {
       thread3Done = true;
   } else if (strcmp(currentThread->GetName(), "4th") == 0) {
       thread4Done = true;
   } else if (strcmp(currentThread->GetName(), "5th") == 0) {
       thread5Done = true;
   }
}




/// Set up a ping-pong between several threads.
///
/// Do it by launching four threads which call `SimpleThread`, and finally
/// calling `SimpleThread` on the current thread (for a total of 5 threads).
void
ThreadTestSimple()
{
   #ifdef SEMAPHORE_TEST
       // Semáforo inicializado en 3: permite hasta 3 hilos simultáneos
       s = new Semaphore("semaforo_simple_test", 3);
   #endif


   Thread *newThread  = new Thread("2nd");
   Thread *newThread2 = new Thread("3rd");
   Thread *newThread3 = new Thread("4th");
   Thread *newThread4 = new Thread("5th");


   newThread ->Fork(SimpleThread, NULL);
   newThread2->Fork(SimpleThread, NULL);
   newThread3->Fork(SimpleThread, NULL);
   newThread4->Fork(SimpleThread, NULL);


   // El hilo "main" también ejecuta la misma función (es el "1st")
   SimpleThread(NULL);


   // Esperar a que todos los hilos secundarios terminen
   while (!thread2Done || !thread3Done || !thread4Done || !thread5Done) {
       currentThread->Yield();
   }


   printf("\nAll threads finished!\n");
   printf("Test finished\n");


#ifdef SEMAPHORE_TEST
   delete s;
#endif
}
