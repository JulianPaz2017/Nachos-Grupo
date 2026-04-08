#include "thread_test_join.hh"
#include "thread.hh"
#include "system.hh"
#include "lib/utility.hh"
#include <stdio.h>

static void 
SimpleChild(void *arg)
{
    const char *name = (const char *)arg;
    DEBUG('t', "Hilo '%s' iniciando ejecucion...\n", name);
    for (int i = 0; i < 5; i++) {
        DEBUG('t', "Hilo '%s' trabajando %d...\n", name, i);
        currentThread->Yield();
    }
    DEBUG('t', "Hilo '%s' terminado.\n", name);
}

void 
ThreadTestJoin()
{
    printf("\n=== Test de Thread::Join ===\n");

    // Caso 1: Join normal (el padre espera al hijo)
    printf("\n--- Caso 1: El padre espera al hijo ---\n");
    Thread *t1 = new Thread("Hijo 1", true);
    t1->Fork(SimpleChild, (void *)"Hijo 1");
    
    printf("Padre esperando a Hijo 1...\n");
    t1->Join();
    printf("Hijo 1 ha sido 'join-eado'. El padre continúa.\n");

    // Caso 2: El hijo termina antes de que el padre llame a Join
    printf("\n--- Caso 2: El hijo termina antes que el Join ---\n");
    Thread *t2 = new Thread("Hijo 2", true);
    t2->Fork(SimpleChild, (void *)"Hijo 2");

    printf("Padre cediendo CPU para que Hijo 2 termine...\n");
    for (int i = 0; i < 20; i++) {
        currentThread->Yield();
    }

    printf("Ahora el padre llama a Join sobre Hijo 2 (que ya debería estar terminado)...\n");
    t2->Join();
    printf("Join de Hijo 2 retornado inmediatamente. Todo OK.\n");

    // Caso 3: Hilos no-joinables (verificar que no rompan nada)
    printf("\n--- Caso 3: Hilos no-joinables (Detached) ---\n");
    Thread *t3 = new Thread("Hijo 3", false);
    t3->Fork(SimpleChild, (void *)"Hijo 3");
    // No llamamos a Join, el scheduler debería borrarlo solo.

    printf("Padre cediendo un poco para ver terminar a Hijo 3...\n");
    for (int i = 0; i < 10; i++) {
        currentThread->Yield();
    }

    printf("\n=== Test de Thread::Join finalizado ===\n");
}
