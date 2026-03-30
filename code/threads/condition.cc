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


#include "condition.hh"
#ifdef PLANCHA2
#include "system.hh"
#endif /* PLANCHA2 */


Condition::Condition(const char *debugName, Lock *conditionLock)
{
    #ifdef PLANCHA2
    ASSERT(conditionLock != nullptr);

    name    = debugName;
    condLock = conditionLock;
    waitQueue = new List<Semaphore *>;

    DEBUG('s', "Variable de condicion '%s' creada (lock asociado: '%s')\n",
          name, condLock->GetName());
    #endif /* PLANCHA2 */
}

Condition::~Condition()
{
    #ifdef PLANCHA2
    // La cola debe estar vacia al destruir: no debe haber hilos esperando.
    ASSERT(waitQueue->IsEmpty());
    delete waitQueue;

    DEBUG('s', "Variable de condicion '%s' destruida\n", name);
    #endif /* PLANCHA2 */
}

const char *
Condition::GetName() const
{
    return name;
}

void
Condition::Wait()
{
    #ifdef PLANCHA2
    // El hilo que llama a Wait debe poseer el lock.
    ASSERT(condLock->IsHeldByCurrentThread());

    DEBUG('s', "Thread '%s' hace Wait en condicion '%s'\n",
          currentThread->GetName(), name);

    // Creamos un semaforo en 0 propio para este hilo y lo encolamos.
    Semaphore *sem = new Semaphore("cond-wait-sem", 0);
    waitQueue->Append(sem);

    // Liberamos el lock atomicamente antes de dormirnos.
    condLock->Release();

    // El hilo se bloquea aqui hasta que alguien haga Signal/Broadcast.
    sem->P();

    // Al despertar, volvemos a adquirir el lock (estilo Mesa).
    condLock->Acquire();

    // Limpiamos el semaforo temporal.
    delete sem;

    DEBUG('s', "Thread '%s' retomo de Wait en condicion '%s'\n",
          currentThread->GetName(), name);
    #endif /* PLANCHA2 */
}

void
Condition::Signal()
{
    #ifdef PLANCHA2
    // El hilo que llama a Signal debe poseer el lock.
    ASSERT(condLock->IsHeldByCurrentThread());

    DEBUG('s', "Thread '%s' hace Signal en condicion '%s'\n",
          currentThread->GetName(), name);

    if (!waitQueue->IsEmpty()) {
        // Despertamos a un solo hilo: sacamos su semaforo y hacemos V().
        Semaphore *sem = waitQueue->Pop();
        sem->V();

        DEBUG('s', "Signal en '%s': un hilo despertado\n", name);
    } else {
        DEBUG('s', "Signal en '%s': no habia hilos esperando\n", name);
    }
    #endif /* PLANCHA2 */
}

void
Condition::Broadcast()
{
    #ifdef PLANCHA2
    // El hilo que llama a Broadcast debe poseer el lock.
    ASSERT(condLock->IsHeldByCurrentThread());

    DEBUG('s', "Thread '%s' hace Broadcast en condicion '%s'\n",
          currentThread->GetName(), name);

    // Despertamos a todos los hilos que esten esperando.
    while (!waitQueue->IsEmpty()) {
        Semaphore *sem = waitQueue->Pop();
        sem->V();
    }

    DEBUG('s', "Broadcast en '%s': todos los hilos despertados\n", name);
    #endif /* PLANCHA2 */
}
