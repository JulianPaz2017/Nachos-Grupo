/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.

#ifndef NACHOS_FILESYS_RAWFILEHEADER__HH
#define NACHOS_FILESYS_RAWFILEHEADER__HH


#include "machine/disk.hh"

// Punteros que entran en un sector:
static const unsigned POINTERS_PER_SECTOR = SECTOR_SIZE / sizeof(unsigned);

// Punteros que caben en el header, dejando lugar para los punteros de indirección simple y doble
static const unsigned NUM_DIRECT
  = (SECTOR_SIZE - 4 * sizeof (int)) / sizeof (int); 

// Tamaño máximo de un archivo (directos +  indirección simple + indirección doble)
const unsigned MAX_FILE_SIZE = (NUM_DIRECT + POINTERS_PER_SECTOR +  POINTERS_PER_SECTOR * POINTERS_PER_SECTOR) * SECTOR_SIZE;

struct RawFileHeader {
    unsigned numBytes;  ///< Number of bytes in the file.
    unsigned numSectors;  ///< Number of data sectors in the file.
    unsigned dataSectors[NUM_DIRECT];  ///< Disk sector numbers for each data
                                       ///< block in the file.

    unsigned singleIndirect; // sector con POINTERS_PER_SECTOR punteros
                             // que apuntan a bloques de datos.
                             
    unsigned doubleIndirect; // sector con POINTERS_PER_SECTOR punteros
                             // que apuntan a sectores de indirección simple.
};


#endif
