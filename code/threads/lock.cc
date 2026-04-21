/// Routines for synchronizing threads.
///
/// The implementation for this primitive does not come with base Nachos.
/// It is left to the student.
///
/// When implementing this module, keep in mind that any implementation of a
/// synchronization routine needs some primitive atomic operation.  The
/// semaphore implementation, for example, disables interrupts in order to
/// achieve this; another way could be leveraging an already existing
/// primitive.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "lock.hh"
#ifdef PLANCHA2
#include <string.h>
#include "system.hh"
#endif /* PLANCHA2 */


/// Dummy functions -- so we can compile our later assignments.

Lock::Lock(const char *debugName)
{
    #ifdef PLANCHA2
    name = debugName;
    lock = new Semaphore(debugName, 1);
    heldedBy = NULL;
    threadPriority = 9;
    #endif /* PLANCHA2 */
}

Lock::~Lock()
{
    #ifdef PLANCHA2
    delete lock;
    #endif /* PLANCHA2 */
}

const char *
Lock::GetName() const
{
    return name;
}

void
Lock::Acquire()
{
    #ifdef PLANCHA2
    // Verificamos si el hilo que llama a Acquire ya posee el lock.
    ASSERT(!IsHeldByCurrentThread());

    // Si el hilo que llama a Acquire no posee el lock, espera para obtenerlo
    DEBUG('t', "The thread called '%s' will acquire the lock called '%s'\n", currentThread->GetName(), name);
    
//    // Desactivamos las interrupciones (para que no hayan race conditions)
//    IntStatus oldLevel = interrupt->SetLevel(INT_OFF);
//
//    // Si el lock está tomado, chequeamos la prioridad del thread que lo tomó
//    if (heldedBy) {
//        unsigned heldedByPriority = heldedBy->GetPriority();
//        unsigned currentThreadPriority = currentThread->GetPriority();
//
//        // Si la prioridad del thread que tiene el lock es mayor
//        // invertimos la prioridad
//        if (currentThreadPriority > heldedByPriority) {
//            // Lo desenconcolamos de la cola de prioridad en la que esté,
//            // ya que su prioridad va a cambiar.
//            for (unsigned i = 0; i <= MAX_PRIORITY; i++) {
//                if (scheduler->Search(heldedBy, i)) {
//                    scheduler->Dequeue(heldedBy, i);
//                    break;
//                }
//            }
//
//            heldedBy->SetPriority(currentThreadPriority);
//            scheduler->ReadyToRun(heldedBy);
//        }
//    }
//    // Volvemos a activar el nivel que tenía anteriormente
//    interrupt->SetLevel(oldLevel);

    scheduler->InvestPriority(heldedBy);

    lock->P();
    DEBUG('t', "'%s' finally acquire '%s'\n", currentThread->GetName(), name);
    threadPriority = currentThread->GetPriority();
    heldedBy = currentThread;

    #endif /* PLANCHA2 */
}

void
Lock::Release()
{
    #ifdef PLANCHA2
    // Verificamos si el hilo que llama a Release posee el lock.
    ASSERT(IsHeldByCurrentThread());

    heldedBy = NULL;

    // Si el hilo posee el lock, entonces lo libera
    DEBUG('t', "The thread called '%s' will release the lock called '%s'\n", currentThread->GetName(), name);

    // Cambiar prioridad.
    currentThread->SetPriority(threadPriority);

    lock->V();
    DEBUG('t', "'%s' release '%s'\n", currentThread->GetName(), name);

    #endif /* PLANCHA2 */
}

bool
Lock::IsHeldByCurrentThread() const
{
    #ifdef PLANCHA2
    return (currentThread == heldedBy);
    #endif /* PLANCHA2 */
    return false;
}
