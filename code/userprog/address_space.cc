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


/// First, set up the translation from program memory to physical memory.

/// Before it was a simple (1:1) mapping, since we were only uniprogramming,
/// and we had a single unsegmented page table.
/// Now we use the global `usedPages` bitmap to allocate physical frames, so that
/// multiple programs can coexist in memory.
AddressSpace::AddressSpace(OpenFile *executable_file)
{
    ASSERT(executable_file != nullptr);

    Executable exe (executable_file);
    ASSERT(exe.CheckMagic());

    // How big is address space?
    unsigned size = exe.GetSize() + USER_STACK_SIZE;
    numPages = DivRoundUp(size, PAGE_SIZE);
    size = numPages * PAGE_SIZE;

    // Verificar que hay suficientes marcos libres
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

        // Limpiamos solamente el marco físico asignado
        memset(&machine->mainMemory[physPage * PAGE_SIZE], 0, PAGE_SIZE);
    }

    char *mainMemory = machine->mainMemory;

#ifdef DEMAND_LOADING
    // Nos guardamos el ejecutable
    executable = executable_file;
    return;
#else
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
    for (unsigned i = 0; i < numPages; i++) {
        usedPages->Clear(pageTable[i].physicalPage);
    }
    delete [] pageTable;

#ifdef DEMAND_LOADING
    delete executable;
#endif
}

void
AddressSpace::LoadPage(unsigned vpn){
    
  return;  
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

    // Marcar cada entrada como inválida.
    for (unsigned i = 0; i < TLB_SIZE; i++) {
        tlb[i].valid = false;
    }
    DEBUG('a', "TLB invalidada en cambio de contexto (SaveState).\n");
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
