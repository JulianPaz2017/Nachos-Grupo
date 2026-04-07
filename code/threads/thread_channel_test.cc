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

static int  sentinelsSent;    // cuántos centinelas se enviaron ya
static Lock *sentinelLock;    // protege sentinelsSent

static int cantidadDeEmisoresActivos=NUM_SENDERS;
static Lock *sendersLock;

static int  globalSum;        // suma acumulada por los receptores
static Lock *sumLock;         // protege globalSum


static void
Sender(void * /* arg */)
{
    while (true) {
        // Tomar el próximo valor a enviar bajo exclusión mutua.
        nextValueLock->Acquire();
        int val = nextValue;
        DEBUG('q', "Tome nextValue %d\n", val);
        if (val > TOTAL_MSGS) {
            nextValueLock->Release();
            DEBUG('q', "Voy a salir\n");
            break;  // ya no quedan datos por enviar
        }
        nextValue++;
        nextValueLock->Release();

        printf(">>> Emisor '%s' va a enviar %d\n",
               currentThread->GetName(), val);
        chan->Send(val);
    }

    // El emisor intenta enviar centinelas (-1) hasta cubrir NUM_RECEIVERS.
    sendersLock->Acquire();
    while (cantidadDeEmisoresActivos == 1 && sentinelsSent < NUM_RECEIVERS) {
        printf(">>> Emisor '%s' va a enviar un centinela (-1)\n",
               currentThread->GetName());
        chan->Send(-1);
    }
    cantidadDeEmisoresActivos--;
    sendersLock->Release();
}


static void
Receiver(void * /* arg */)
{
    int localSum = 0;
    int val;

    while (true) {
        chan->Receive(&val);

        if (val == -1) {
            // Centinela: este receptor termina.
            printf("<<< Receptor '%s' recibió centinela, termina (parcial=%d)\n",
                   currentThread->GetName(), localSum);

            break;
        }

        printf("<<< Receptor '%s' recibió %d\n",
               currentThread->GetName(), val);
        localSum += val;
    }

    // Acumular la suma local en la variable global.
    sumLock->Acquire();
    globalSum += localSum;
    sumLock->Release();
    
    sentinelLock->Acquire();
    sentinelsSent++;
    sentinelLock->Release();
}


void
ThreadTestChannel()
{
    printf("=== Channel Buffer Test (msgs=%d, senders=%d, receivers=%d) ===\n",
           TOTAL_MSGS, NUM_SENDERS, NUM_RECEIVERS);

    // Inicialización.
    chan           = new Channel("bufChan");
    nextValueLock = new Lock("nextValueLock");
    sentinelLock  = new Lock("sentinelLock");
    sumLock       = new Lock("sumLock");
    sendersLock   = new Lock("sendersLock");

    nextValue    = 1;
    sentinelsSent = 0;
    globalSum    = 0;

    // Suma esperada: 1 + 2 + ... + TOTAL_MSGS.
    int expectedSum = TOTAL_MSGS * (TOTAL_MSGS + 1) / 2;

    // Crear receptores (primero para que estén listos al recibir).
    char rName[32];
    for (int r = 0; r < NUM_RECEIVERS; r++) {
        snprintf(rName, sizeof rName, "Receiver-%d", r);
        Thread *t = new Thread(rName);
        t->Fork(Receiver, nullptr);
    }

    // Crear emisores.
    char sName[32];
    for (int s = 0; s < NUM_SENDERS; s++) {
        snprintf(sName, sizeof sName, "Sender-%d", s);
        Thread *t = new Thread(sName);
        t->Fork(Sender, nullptr);
    }

    // Esperar a que todos terminen (scheduling cooperativo).
    while (sentinelsSent < NUM_RECEIVERS && cantidadDeEmisoresActivos > 0) {
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
    delete sentinelLock;
    delete sumLock;
    delete sendersLock;

    printf("=== Channel Buffer Test finalizado ===\n");
}
