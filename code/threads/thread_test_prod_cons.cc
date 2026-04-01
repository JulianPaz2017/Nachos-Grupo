/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2007-2009 Universidad de Las Palmas de Gran Canaria.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "thread_test_prod_cons.hh"

#include "semaphore.hh"
#include "lock.hh"
#include "condition.hh"
#include "system.hh"
#include "Channel.hh" // Agregamos para testear canales
#include <stdio.h>


static int buffer[BUFFER_SIZE];  // buffer 
static int in  = 0;              // proxima posicion de escritura
static int out = 0;              // proxima posicion de lectura
static int count = 0;            // cantidad de items en el buffer

// Variables de condicion para sincronizar productor y consumidor.
static Condition *notEmpty; // consumidor espera aqui si buffer esta vacio
static Condition *notFull;  // productor espera aqui si buffer esta lleno

// Mutex: protege la manipulacion de los indices in/out y el contador.
static Lock *mutex;


static void Producer(void * /* arg */)
{
    for (int i = 1; i <= NUM_ITEMS; i++) {

        mutex->Acquire();
        while (count == BUFFER_SIZE) {
            printf("Productor esperando (buffer lleno)\n");
            notFull->Wait();
        }

        int pos    = in;
        buffer[in] = i;
        in         = (in + 1) % BUFFER_SIZE;
        count++;

        printf("Productor produce: %d en %d\n", i, pos);

        notEmpty->Signal();
        mutex->Release();
    }
}

static void Consumer(void * /* arg */)
{
    for (int i = 1; i <= NUM_ITEMS; i++) {

        mutex->Acquire();
        while (count == 0) {
            printf("Consumidor esperando (buffer vacio)\n");
            notEmpty->Wait();
        }

        int pos  = out;
        int item = buffer[out];
        out      = (out + 1) % BUFFER_SIZE;
        count--;

        // Se usa `item` o bien `i` puesto que son iguales, pero para 
        // respetar estrictamente el output solicitado:
        printf("Consumidor consume: %d en %d\n", i, pos);

        notFull->Signal();
        mutex->Release();
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// Test opcional 1: dos hilos incrementan un contador con un Lock.
//   Verifica que el lock protege correctamente la seccion critica.
//   Sin el lock, el contador final seria menor que 2 * LOCK_TEST_ITERS
//   debido a actualizaciones perdidas por interleaving.
// ─────────────────────────────────────────────────────────────────────────────

#define LOCK_TEST_ITERS  500

static int   sharedCounter = 0;
static Lock *counterLock;

static void LockTestThread(void * /* arg */)
{
    for (int i = 0; i < LOCK_TEST_ITERS; i++) {
        counterLock->Acquire();
        sharedCounter++;
        counterLock->Release();
    }
}

static void LockTest()
{
    printf("\n=== Lock Test (2 hilos x %d incrementos, esperado=%d) ===\n",
           LOCK_TEST_ITERS, 2 * LOCK_TEST_ITERS);

    sharedCounter = 0;
    counterLock   = new Lock("counterLock");

    Thread *t1 = new Thread("LockThread-1");
    Thread *t2 = new Thread("LockThread-2");
    t1->Fork(LockTestThread, nullptr);
    t2->Fork(LockTestThread, nullptr);

    // Cedemos la CPU para que los hilos terminen antes de imprimir.
    // En Nachos con scheduling cooperativo alcanza con un Yield por cada
    // iteracion que hacen los hilos forkeados; como son 500 c/u hacemos
    // varios yields desde el hilo principal.
    for (int i = 0; i < 2 * LOCK_TEST_ITERS + 10; i++)
        currentThread->Yield();

    printf("Contador final: %d (correcto=%s)\n",
           sharedCounter,
           sharedCounter == 2 * LOCK_TEST_ITERS ? "SI" : "NO");

    delete counterLock;
}


// ─────────────────────────────────────────────────────────────────────────────
// Test opcional 2: variable de condicion estilo Mesa.
//   Un hilo "waiter" espera en una condicion; otro hilo "signaler"
//   modifica el estado compartido y hace Signal.  Verifica que el waiter
//   solo avanza cuando la condicion es verdadera.
// ─────────────────────────────────────────────────────────────────────────────

static Lock      *condTestLock;
static Condition *condVar;
static bool       condReady = false;   // estado compartido protegido por condTestLock

static void CondWaiter(void * /* arg */)
{
    condTestLock->Acquire();

    // Bucle while: idioma correcto para variables de condicion estilo Mesa.
    // Re-chequeamos la condicion al despertar porque otro hilo pudo
    // haberla consumido antes que nosotros (spurious wakeup o Broadcast).
    while (!condReady) {
        printf("Waiter: condicion falsa, esperando...\n");
        condVar->Wait();  // libera el lock y duerme; al volver lo readquiere
    }

    printf("Waiter: condicion verdadera, continua\n");
    condTestLock->Release();
}

static void CondSignaler(void * /* arg */)
{
    // Cedemos un poco para darle tiempo al waiter de bloquearse primero.
    for (int i = 0; i < NUM_ITEMS; i++)
        currentThread->Yield();

    condTestLock->Acquire();
    condReady = true;
    printf("Signaler: estado cambiado, enviando Signal\n");
    condVar->Signal();
    condTestLock->Release();
}

static void ConditionTest()
{
    printf("\n=== Condition Variable Test ===\n");

    condReady    = false;
    condTestLock = new Lock("condTestLock");
    condVar      = new Condition("condVar", condTestLock);

    Thread *waiter   = new Thread("Waiter");
    Thread *signaler = new Thread("Signaler");

    waiter->Fork(CondWaiter,   nullptr);
    signaler->Fork(CondSignaler, nullptr);

    // Cedemos para que ambos hilos terminen.
    for (int i = 0; i < NUM_ITEMS * 1.2; i++)
        currentThread->Yield();

    delete condVar;
    delete condTestLock;

    printf("=== Condition Variable Test finalizado ===\n");
}


// Agregamos para testear Canales:

// ─────────────────────────────────────────────────────────────────────────────
// Test opcional 3: Canal sincrónico (Rendezvous).
//   Verifica que Send/Receive se bloqueen mutuamente hasta que ambos coincidan.
// ─────────────────────────────────────────────────────────────────────────────

static Channel *testChannel;

static void ChannelProducer(void * /* arg */)
{
    for (int i = 1; i <= 5; i++) {
        printf(">>> [Canal] Productor intentando enviar: %d\n", i);
        testChannel->Send(i);
        printf("<<< [Canal] Productor envió: %d correctamente\n", i);
    }
}

static void ChannelConsumer(void * /* arg */)
{
    int val = 0;
    for (int i = 1; i <= 5; i++) {
        printf("--- [Canal] Consumidor listo para recibir...\n");
        testChannel->Receive(&val);
        printf("+++ [Canal] Consumidor recibió: %d\n", val);
    }
}

static void ChannelTest()
{
    printf("\n=== Synchronous Channel Test (Rendezvous) ===\n");

    testChannel = new Channel("testChannel");

    Thread *p = new Thread("ChannelProducer");
    Thread *c = new Thread("ChannelConsumer");

    p->Fork(ChannelProducer, nullptr);
    c->Fork(ChannelConsumer, nullptr);

    // Yield suficiente para que terminen las transferencias.
    for (int i = 0; i < NUM_ITEMS * 1.2; i++)
        currentThread->Yield();

    delete testChannel;
    printf("=== Synchronous Channel Test finalizado ===\n");
}


// ─────────────────────────────────────────────────────────────────────────────
// Punto de entrada principal
// ─────────────────────────────────────────────────────────────────────────────


void
ThreadTestProdCons()
{
    // ── Productor / Consumidor ──────────────────────────────────────────────
    printf("===` Producer-Consumer Test (buffer=%d, items=%d) ===\n",
           BUFFER_SIZE, NUM_ITEMS);

    in  = 0;
    out = 0;
    count = 0;
    mutex = new Lock("mutex");
    notEmpty = new Condition("notEmpty", mutex);
    notFull  = new Condition("notFull", mutex);

    // Creamos el consumidor primero para que este listo cuando lleguen items.
    Thread *consumer = new Thread("Consumer");
    consumer->Fork(Consumer, nullptr);

    Thread *producer = new Thread("Producer");
    producer->Fork(Producer, nullptr);

    // El hilo principal cede la CPU; el scheduler se encarga del resto.
    // Cuando productor y consumidor terminen, Nachos volvera aqui.
    currentThread->Yield();

    // ── Tests opcionales ────────────────────────────────────────────────────
    //LockTest();
    //ConditionTest();
    ChannelTest();  // Agregamos nuestra nueva prueba de canales
}
