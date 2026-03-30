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
    lock->P();
    DEBUG('t', "'%s' finally acquire '%s'\n", currentThread->GetName(), name);

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
