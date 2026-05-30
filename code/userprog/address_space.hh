/// Data structures to keep track of executing user programs (address
/// spaces).
///
/// For now, we do not keep any information about address spaces.  The user
/// level CPU state is saved and restored in the thread executing the user
/// program (see `thread.hh`).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.

#ifndef NACHOS_USERPROG_ADDRESSSPACE__HH
#define NACHOS_USERPROG_ADDRESSSPACE__HH


#include "filesys/file_system.hh"
#include "machine/translation_entry.hh"
#include "machine/mmu.hh"  ///< Necesario para TLB_SIZE en SaveState.
#include "executable.hh"


const unsigned USER_STACK_SIZE = 1024;  ///< Increase this as necessary!


class AddressSpace {
public:

    /// Create an address space to run a user program.
    ///
    /// The address space is initialized from an already opened file.
    /// The program contained in the file is loaded into memory and
    /// everything is set up so that user instructions can start to be
    /// executed.
    ///
    /// Parameters:
    /// * `executable_file` is the open file that corresponds to the
    ///   program; it contains the object code to load into memory.
    AddressSpace(OpenFile *executable_file);

    /// De-allocate an address space.
    ~AddressSpace();

    /// Initialize user-level CPU registers, before jumping to user code.
    void InitRegisters();

    /// Save/restore address space-specific info on a context switch.

    void SaveState();
    void RestoreState();

    /// Retorna un puntero de solo lectura a la tabla de páginas del proceso.
    /// Usado por el PageFaultHandler para cargar entradas faltantes en la TLB.
    const TranslationEntry *GetPageTable() const { return pageTable; }

    /// Retorna la cantidad de páginas virtuales de este espacio de direcciones.
    unsigned GetNumPages() const { return numPages; }

private:

    /// Assume linear page table translation for now!
    TranslationEntry *pageTable;

    /// Number of pages in the virtual address space.
    unsigned numPages;

#ifdef DEMAND_LOADING
    /// El ejecutable se mantiene abierto para carga por demanda.
    OpenFile *executableFile;

    /// Tipo de segmento que respalda cada página virtual.
    /// Usado por LoadPage() para saber qué leer del archivo.
    enum PageSegment {
        SEG_CODE,       ///< Segmento de código (read-only).
        SEG_INIT_DATA,  ///< Segmento de datos inicializados.
        SEG_UNINIT,     ///< Datos no inicializados (solo ceros).
        SEG_STACK       ///< Pila (solo ceros, sin respaldo en archivo).
    };

    PageSegment *pageSegments; ///< Tipo de segmento por página.
    uint32_t    *pageOffsets;  ///< Offset dentro del segmento para esta página.

public:
    /// Carga bajo demanda la página virtual `vpn` en un marco físico libre.
    /// Llamado desde el PageFaultHandler cuando la entrada de la pageTable
    /// tiene valid=false.
    void LoadPage(unsigned vpn);

private:
#endif

};


#endif
