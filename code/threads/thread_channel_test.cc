// thread_channel_buffer_test.cc
// Test: N emisores envían números (1..TOTAL_MSGS) por un canal a M receptores.
// Cada receptor suma los valores recibidos en una variable global `globalSum`.
// Al final se verifica que globalSum == Σ(1..TOTAL_MSGS).
//
// Protocolo de terminación: los emisores, después de enviar todos los datos,
// envían NUM_RECEIVERS mensajes centinela (valor -1) para que cada receptor
// sepa cuándo detenerse.

#include "thread_channel_test.hh"

#include "Channel.hh"
#include "lock.hh"
#include "system.hh"
#include <stdio.h>


static Channel *chan;         // canal de comunicación

static int  nextValue;        // próximo valor a enviar
static Lock *nextValueLock;   // protege nextValue

static int  activeReceivers = NUM_RECEIVERS; // cuántos centinelas se enviaron ya
static Lock *receiversLock;                  // protege sentinelsSent

static int activeSenders = NUM_SENDERS; // Cantidad de emisores activos
static Lock *sendersLock;               // Lock para los emisores activos

static int  globalSum;        // suma acumulada por los receptores
static Lock *sumLock;         // protege globalSum


static void
Sender(void*) 
{
    int val;

    do {
        nextValueLock->Acquire();
        val = nextValue;
        DEBUG('q', "Tome el valor %d\n", val);

        if (val <= TOTAL_MSGS) {
            DEBUG('q', ">>  Emisor '%s' va a enviar %d\n",
                currentThread->GetName(), val);
            chan->Send(val);
            currentThread->Yield(); 
            nextValue++;
        }
        nextValueLock->Release();
        currentThread->Yield(); 
    } while(val <= TOTAL_MSGS);

    sendersLock->Acquire();
    if (activeSenders == 1) {
        DEBUG('q', "Limpiando receptores\n");
        for (int i = 0; i < NUM_RECEIVERS ;i++)
            chan->Send(-1);
    }
    activeSenders--;
    DEBUG('q', "El valor de activeSenders es %d\n", activeSenders);
    sendersLock->Release();

    DEBUG('q', "Dando de baja el emisor '%s'\n",
        currentThread->GetName());
}

static void
Receiver(void*)
{
    int localSum = 0;
    int val;

    do {
        chan->Receive(&val);
        currentThread->Yield(); 

        DEBUG('q', "<< Receptor '%s' recibió %d\n", 
                currentThread->GetName(), val);

        if (val >= 0) {
            localSum += val;
        }

    } while (val != -1);

    // Acumular la suma local en la variable global.
    sumLock->Acquire();
    globalSum += localSum;
    sumLock->Release();

    DEBUG('q', "Dando de baja el receptor '%s'\n",
        currentThread->GetName());

    receiversLock->Acquire();
    activeReceivers--;
    receiversLock->Release();
}

void
ThreadTestChannel()
{
    printf("=== Channel Buffer Test (msgs=%d, senders=%d, receivers=%d) ===\n",
           TOTAL_MSGS, NUM_SENDERS, NUM_RECEIVERS);

    // Inicialización.
    chan          = new Channel("bufChan");
    nextValueLock = new Lock("nextValueLock");
    sumLock       = new Lock("sumLock");
    sendersLock   = new Lock("sendersLock");
    receiversLock = new Lock("receiversLock");

    nextValue    = 1;
    globalSum    = 0;

    // Suma esperada: 1 + 2 + ... + TOTAL_MSGS.
    int expectedSum = TOTAL_MSGS * (TOTAL_MSGS + 1) / 2;

    // Crear receptores (primero para que estén listos al recibir).
    for (int r = 0; r < NUM_RECEIVERS; r++) {
        char *rName = new char[32]; 
        snprintf(rName, 32, "Receiver-%d", r);
        Thread *t = new Thread(rName);
        t->Fork(Receiver, nullptr);
    }

    // Crear emisores.
    for (int s = 0; s < NUM_SENDERS; s++) {
        char *sName = new char[32]; 
        snprintf(sName, 32, "Sender-%d", s);
        Thread *t = new Thread(sName);
        t->Fork(Sender, nullptr);
    }

    // Esperar a que todos terminen (scheduling cooperativo).
    while (activeSenders > 0 && activeReceivers > 0) {
        currentThread->Yield();
    }


    // Verificación.
    printf("\n--- Resultado ---\n");
    printf("Suma global:   %d\n", globalSum);
    printf("Suma esperada: %d\n", expectedSum);
    printf("Test: %s\n",
           globalSum == expectedSum ? "CORRECTO" : "INCORRECTO");

    // Limpieza.
    delete chan;
    delete nextValueLock;
    delete sumLock;
    delete sendersLock;
    delete receiversLock;
    
    printf("=== Channel Buffer Test finalizado ===\n");
}
