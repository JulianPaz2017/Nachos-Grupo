/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2007-2009 Universidad de Las Palmas de Gran Canaria.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "thread_test_prod_cons.hh"

#include "lock.hh"
#include "condition.hh"
#include "system.hh"
#include "lib/utility.hh"
#include <stdio.h>


static int buffer[BUFFER_SIZE];  // buffer 
static int in  = 0;              // proxima posicion de escritura
static int out = 0;              // proxima posicion de lectura
static int count = 0;            // cantidad de items en el buffer

static bool producerFinish = false; // Variable para verificar que el productor finalizó
static bool consumerFinish = false; // Variable para verificar que el consumidor finalizó

// Variables de condicion para sincronizar productor y consumidor.
static Condition *notEmpty; // consumidor espera aqui si buffer esta vacio
static Condition *notFull;  // productor espera aqui si buffer esta lleno

// Mutex: protege la manipulacion de los indices in/out y el contador.
static Lock *mutex;


static void Producer(void * /* arg */)
{
    for (int i = 1; i <= NUM_ITEMS; i++) {
        currentThread->Yield();
        mutex->Acquire();
        while (count == BUFFER_SIZE) {
            DEBUG('t', "Productor esperando (buffer lleno)\n");
            notFull->Wait();
        }

        int pos    = in;
        buffer[in] = i;
        in         = (in + 1) % BUFFER_SIZE;
        count++;

        DEBUG('t', "Productor produce: %d en %d\n", i, pos);

        notEmpty->Signal();
        mutex->Release();
    }

    producerFinish = true;
}

static void Consumer(void * /* arg */)
{
    int totalValue = 0;

    for (int i = 1; i <= NUM_ITEMS; i++) {
        currentThread->Yield();
        mutex->Acquire();
        while (count == 0) {
            DEBUG('t', "Consumidor esperando (buffer vacio)\n");
            notEmpty->Wait();
        }

        int pos  = out;
        int item = buffer[out];

        out      = (out + 1) % BUFFER_SIZE;
        count--;
        totalValue += item;

        DEBUG('t', "Consumidor consume: %d en %d\n", item, pos);

        notFull->Signal();
        mutex->Release();
    }

    printf("El valor final es: %d\n", totalValue);

    consumerFinish = true;
}

void
ThreadTestProdCons()
{
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

    // Cedemos para que ambos hilos terminen.
    while (!producerFinish || !consumerFinish)
        currentThread->Yield();

    delete notEmpty;
    delete notFull;
    delete mutex;
}
