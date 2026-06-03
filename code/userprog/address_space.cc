/// Routines to manage address spaces (memory for executing user programs).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "address_space.hh"
#include "executable.hh"
#include "threads/system.hh"

#include <string.h>

#include <algorithm>


/// First, set up the translation from program memory to physical memory.

/// Before it was a simple (1:1) mapping, since we were only uniprogramming,
/// and we had a single unsegmented page table.
/// Now we use the global `usedPages` bitmap to allocate physical frames, so that
/// multiple programs can coexist in memory.
AddressSpace::AddressSpace(OpenFile *executable_file, int pid)
{
    ASSERT(executable_file != nullptr);

    Executable exe (executable_file);
    ASSERT(exe.CheckMagic());

    // How big is address space?
    unsigned size = exe.GetSize() + USER_STACK_SIZE;
    numPages = DivRoundUp(size, PAGE_SIZE);
    size = numPages * PAGE_SIZE;

#ifdef SWAP
    /// Creamos el archivo SWAP.X
    sprintf(swapName, "SWAP.%d", pid);
    
    /// Creamos el archivo SWAP.X, si falla terminamos el nachos
    if (!fileSystem->Create(swapName, size)) {
        DEBUG('a', "Error: No se pudo crear el archivo de swap %s\n", swapName);
        ASSERT(false);
    }

    // Abrimos el archivo
    swapFile = fileSystem->Open(swapName);
    ASSERT(swapFile != nullptr);
    
    DEBUG('a', "Archivo de SWAP creado: %s con tamaño %u\n", swapName, swapSize);
#endif

#ifdef DEMAND_LOADING
    // Creamos la translationEntry
    pageTable = new TranslationEntry[numPages];
    for (unsigned i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
        pageTable[i].physicalPage = -1;
        pageTable[i].valid        = false; 
        pageTable[i].use          = false;
        pageTable[i].dirty        = false;
        pageTable[i].readOnly     = false;

        #ifdef SWAP
        /// Seteamos esto en false ya que la página nunca fue cargada en RAM
        pageTable[i].alredyLoaded = false;
        #endif
    }

    /// Nos guardamos el ejecutable
    executable = executable_file;
#else
    // Verificar que hay suficientes marcos libres (solo sin carga por demanda)
    ASSERT(numPages <= usedPages->CountClear());

    // First, set up the translation: asignamos marcos físicos usando el bitmap.
    pageTable = new TranslationEntry[numPages];
    for (unsigned i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
        // Buscar un marco físico libre
        int physPage = usedPages->Find();
        ASSERT(physPage != -1);
        pageTable[i].physicalPage = physPage;
        pageTable[i].valid        = true;
        pageTable[i].use          = false;
        pageTable[i].dirty        = false;
        pageTable[i].readOnly     = false;

        #ifdef SWAP
        /// Seteamos esto en true ya que todas las páginas fueron cargadas en la RAM
        pageTable[i].alredyLoaded = true;
        #endif

        // Limpiamos solamente el marco físico asignado
        memset(&machine->mainMemory[physPage * PAGE_SIZE], 0, PAGE_SIZE);
    }

    char *mainMemory = machine->mainMemory;

    // Then, copy in the code and data segments into memory.
    uint32_t codeSize = exe.GetCodeSize();
    uint32_t initDataSize = exe.GetInitDataSize();
    if (codeSize > 0) {
        uint32_t virtualAddr = exe.GetCodeAddr();
        DEBUG('a', "Initializing code segment, at 0x%X, size %u\n",
              virtualAddr, codeSize);
        for (uint32_t j = 0; j < codeSize; j++) {
            uint32_t vPage = (virtualAddr + j) / PAGE_SIZE;
            uint32_t offset = (virtualAddr + j) % PAGE_SIZE;
            uint32_t physAddr = pageTable[vPage].physicalPage * PAGE_SIZE + offset;
            char byte;
            exe.ReadCodeBlock(&byte, 1, j);
            mainMemory[physAddr] = byte;
        }
    }
    if (initDataSize > 0) {
        uint32_t virtualAddr = exe.GetInitDataAddr();
        DEBUG('a', "Initializing data segment, at 0x%X, size %u\n",
              virtualAddr, initDataSize);
        for (uint32_t j = 0; j < initDataSize; j++) {
            uint32_t vPage = (virtualAddr + j) / PAGE_SIZE;
            uint32_t offset = (virtualAddr + j) % PAGE_SIZE;
            uint32_t physAddr = pageTable[vPage].physicalPage * PAGE_SIZE + offset;
            char byte;
            exe.ReadDataBlock(&byte, 1, j);
            mainMemory[physAddr] = byte;
        }
    }
#endif
}

/// Deallocate an address space.
/// Liberamos los marcos físicos en el bitmap global.
AddressSpace::~AddressSpace()
{
    if (pageTable != nullptr) {
        for (unsigned i = 0; i < numPages; i++) {
            if (pageTable[i].valid) { 
                usedPages->Clear(pageTable[i].physicalPage);
            }
        }
        delete [] pageTable;
    }

#ifdef DEMAND_LOADING
    delete executable;
#endif

#ifdef SWAP
    delete swapFile;
#endif 

}


#if defined(SWAP) || defined(DEMAND_LOADING)
void
AddressSpace::LoadPage(unsigned vpn)
{
    /// Chequeos de seguridad
    ASSERT(vpn < numPages);
    ASSERT(!pageTable[vpn].valid);

    /// Diferenciamos el caso de SWAP o no SWAP ya que la
    /// máquina trabaja con estructuras distintas
    #ifdef SWAP
        /// Buscamos un marco físico libre el en coremap
        int freePpn = coremap->Find();

        /// Si no hay ninguna página libre, liberamos una página ocupada
        if (freePpn == -1) {
            /// Obtenemos el marco físico que vamos a reemplazar
            int freePpn = coremap->PickVictim();

            /// Guardamos el marco físico en el archivo SWAP del proceso
            coremap->SavePage(freePpn);

            /// Limpiamos la entrada del coremap
            coremap->Clear(freePpn);

            /// Limpiamos el marco físico
            char *physAddr = &machine->mainMemory[freePpn * PAGE_SIZE];
            memset(physAddr, 0, PAGE_SIZE);
        }

        #ifdef DEMAND_LOADING
        /// Si ya fue cargado alguna vez en memoria, cargo la página desde el archivo SWAP
        if (pageTable[vpn].alredyLoaded) FromSwapFile(vpn, freePpn, physAddr);

        /// En el caso contrario, lo cargo desde el ejecutable
        else LoadFromExecutable(vpn, freePpn, physAddr);

        #else
        /// Si no utilizamos DEMAND_LOADING, la página seguro está en el archivo SWAP
        FromSwapFile(vpn, freePpn, physAddr);
        #endif

        /// Marcamos el coremap con la nueva página cargada
        coremap->Mark(freePpn, vpn, currentThread->GetPid());
    #else
        /// Buscamos un marco físico libre en el bitmap
        int freePpn = usedPages->Find();
        ASSERT(freePpn != -1);

        /// Limpiamos el marco físico asignado 
        char *physAddr = &machine->mainMemory[freePpn * PAGE_SIZE];
        memset(physAddr, 0, PAGE_SIZE);

        /// Una vez que el marco físico está limpio, cargamos en memoria RAM la página virtual
        LoadFromExecutable(vpn, freePpn, physAddr);
    #endif

    // Actualizamos la tabla de páginas del proceso
    pageTable[vpn].physicalPage = freePpn;
    pageTable[vpn].valid        = true;
    pageTable[vpn].use          = false;
    pageTable[vpn].dirty        = false;
    pageTable[vpn].readOnly     = false;
    
    #ifdef SWAP
    pageTable[vpn].alredyLoaded = true;
    #endif
}
#endif


void FromSwapFile(unsigned vpn, int ppn, char* ppAddr) 
{
    return;
}

void LoadFromExecutable(unsigned vpn, int ppn, char* ppAddr) 
{
    // Cargamos los datos desde el ejecutable si la página virtual intersecta con el código o datos inicializados
    Executable exe(executable);
    uint32_t pageStartVA = vpn * PAGE_SIZE; // inicio de la página virtual

    // Intersección con el Segmento de Código
    uint32_t codeStart = exe.GetCodeAddr();
    uint32_t codeSize  = exe.GetCodeSize();
    uint32_t codeIntersectStart = std::max(pageStartVA, codeStart);
    uint32_t codeIntersectEnd   = std::min(pageStartVA + PAGE_SIZE, codeStart + codeSize);

    if (codeIntersectStart < codeIntersectEnd) {
        uint32_t offsetInSegment = codeIntersectStart - codeStart;
        uint32_t sizeToRead      = codeIntersectEnd - codeIntersectStart;
        uint32_t destOffset      = codeIntersectStart - pageStartVA;
        exe.ReadCodeBlock(ppAddr + destOffset, sizeToRead, offsetInSegment);
    }

    // Intersección con el Segmento de Datos Inicializados
    uint32_t initDataStart = exe.GetInitDataAddr();
    uint32_t initDataSize  = exe.GetInitDataSize();
    uint32_t dataIntersectStart = std::max(pageStartVA, initDataStart);
    uint32_t dataIntersectEnd   = std::min(pageStartVA + PAGE_SIZE, initDataStart + initDataSize);

    if (dataIntersectStart < dataIntersectEnd) {
        uint32_t offsetInSegment = dataIntersectStart - initDataStart;
        uint32_t sizeToRead      = dataIntersectEnd - dataIntersectStart;
        uint32_t destOffset      = dataIntersectStart - pageStartVA;
        exe.ReadDataBlock(ppAddr + destOffset, sizeToRead, offsetInSegment);
    }

    DEBUG('a', "DEMAND LOADING: Página virtual %u cargada exitosamente en el marco físico %d.\n",
          vpn, ppn);
}


/// Set the initial values for the user-level register set.
///
/// We write these directly into the “machine” registers, so that we can
/// immediately jump to user code.  Note that these will be saved/restored
/// into the `currentThread->userRegisters` when this thread is context
/// switched out.
void
AddressSpace::InitRegisters()
{
    for (unsigned i = 0; i < NUM_TOTAL_REGS; i++) {
        machine->WriteRegister(i, 0);
    }

    // Initial program counter -- must be location of `Start`.
    machine->WriteRegister(PC_REG, 0);

    // Need to also tell MIPS where next instruction is, because of branch
    // delay possibility.
    machine->WriteRegister(NEXT_PC_REG, 4);

    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we do not
    // accidentally reference off the end!
    machine->WriteRegister(STACK_REG, numPages * PAGE_SIZE - 16);
    DEBUG('a', "Initializing stack register to %u\n",
          numPages * PAGE_SIZE - 16);
}

/// On a context switch, save any machine state, specific to this address
/// space, that needs saving.
///
/// Con TLB habilitada: invalidamos todas las entradas de la TLB porque
/// pertenecen al proceso que se suspende. La TLB es un recurso global de
/// hardware, y sus traducciones son válidas solo para el espacio de
/// direcciones actual. Si no las invalidamos, el próximo proceso podría
/// usar traducciones de otro proceso → corrupción de memoria.
void
AddressSpace::SaveState()
{
#ifdef USE_TLB
    // Obtener la TLB desde la MMU (hardware simulado).
    TranslationEntry *tlb = machine->GetMMU()->tlb;

    // Marcar cada entrada como inválida y guardar los bits dirty y use en la tabla de páginas.
    for (unsigned i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].valid) {
            tlb[i].valid = false;
            #ifdef SWAP
            unsigned vpn = tlb[i].virtualPage;
            pageTable[vpn].dirty = tlb[i].dirty;
            pageTable[vpn].use   = tlb[i].use;
            #endif
        }
    DEBUG('a', "TLB invalidada en cambio de contexto (SaveState).\n");
    }
#endif
    // Sin TLB: no hay estado adicional que guardar en este punto.

}

/// On a context switch, restore the machine state so that this address space
/// can run.
///
/// Con TLB: la MMU usa la TLB (hardware), no la pageTable directamente.
/// Las traducciones se cargarán en la TLB bajo demanda mediante el
/// PageFaultHandler. No hay nada que configurar aquí: la TLB ya fue
/// invalidada por SaveState.
///
/// Sin TLB: apuntamos la MMU a la tabla de páginas de este proceso para
/// que pueda traducir direcciones virtuales a físicas directamente.
void
AddressSpace::RestoreState()
{
#ifdef USE_TLB
    // Modo TLB: la MMU usa tlb[], no pageTable.
    // Las entradas se cargan por fallo (PageFaultHandler).
    DEBUG('a', "RestoreState en modo TLB: sin carga de pageTable.\n");
#else
    // Modo tabla de páginas lineal: indicarle a la MMU cuál tabla usar.
    machine->GetMMU()->pageTable     = pageTable;
    machine->GetMMU()->pageTableSize = numPages;
#endif
}
