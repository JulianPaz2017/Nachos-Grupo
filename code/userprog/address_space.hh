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


const unsigned USER_STACK_SIZE = 1024;  ///< Increase this as necessary!

class Bitmap;

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
    AddressSpace(OpenFile *executable_file, int pid);

    /// De-allocate an address space.
    ~AddressSpace();

    /// Initialize user-level CPU registers, before jumping to user code.
    void InitRegisters();

    /// Save/restore address space-specific info on a context switch.
    void SaveState();
    void RestoreState();

    /// Retorna un puntero de solo lectura a la tabla de páginas del proceso.
    /// Usado por el PageFaultHandler para cargar entradas faltantes en la TLB.
    TranslationEntry *GetPageTable();

    /// Getter de la cantidad de páginas
    unsigned GetNumPages() const;

    /// Getter del PID
    int GetPid();

    /// Carga una página virtual en memoria física
    void LoadPage(unsigned vpn);

    /// Getter del archivo ejecutable
    OpenFile* GetExecutableFile();

    #ifdef SWAP
    /// Getter de la shadowTable
    Bitmap* GetShadowTable();

    /// Carga desde el archivo SWAP
    void LoadFromSwapFile(unsigned vpn, int ppn, char* ppAddr);

    /// Escribe una página virtual en SWAP.pid
    void WriteToSwap(unsigned vpn, char* physicalAddress);
    #endif
    
    /// Carga desde el ejecutable
    void LoadFromExecutable(unsigned vpn, int ppn, char* ppAddr);

private:

    /// Assume linear page table translation for now!
    TranslationEntry *pageTable;

    /// Number of pages in the virtual address space.
    unsigned numPages;

    /// PID del thread dueño de este espacio de direcciones
    int pId;

    // Almacenamos el ejecutable por si tenemos que cargar más información del mismo
    OpenFile *executableFile;

    #ifdef SWAP
    /// Archivo SWAP.X
    OpenFile *swapFile;
    char swapName[16];

    /// Bitmap para determinar si una página está en el ejecutable/SWAP.pid
    Bitmap *shadowTable;
    #endif
};


#endif
