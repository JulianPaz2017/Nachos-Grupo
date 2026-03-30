/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2007-2009 Universidad de Las Palmas de Gran Canaria.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.

// prod_cons.hh
// Interface for the Producer-Consumer test in NachOS.
// Uses a bounded buffer of size 3, semaphores for sync,
// and a Lock (mutex) to protect the buffer index.

#ifndef PROD_CONS_H
#define PROD_CONS_H

#define BUFFER_SIZE   3
#define NUM_ITEMS     1000

void ThreadTestProdCons();

#endif