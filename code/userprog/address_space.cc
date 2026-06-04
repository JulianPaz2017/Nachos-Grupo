/// Routines to manage address spaces (memory for executing user programs).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "address_space.hh"
#include "executable.hh"
#include "threads/system.hh"

#include "lib/coremap.hh"

#include <string.h>
#include <algorithm>


AddressSpace::AddressSpace(OpenFile *executable_file, int pid)
{
    ASSERT(executable_file != nullptr);
    ASSERT(pid != -1);

    Executable exe (executable_file);
    ASSERT(exe.CheckMagic());

    /// How big is address space?
    unsigned size = exe.GetSize() + USER_STACK_SIZE;
    this->numPages = DivRoundUp(size, PAGE_SIZE);
    size = numPages * PAGE_SIZE;

    /// Inicializamos el PID
    this->pId = pid;

    /// Guardamos el ejecutable
    this->executableFile = executable_file;

    /// Si está definido SWAP, creamos el archivo SWAP.pid
    #ifdef SWAP
    sprintf(this->swapName, "SWAP.%d", pid);
    
    /// Creamos el archivo SWAP.X, si falla terminamos el nachos
    if (!fileSystem->Create(swapName, size)) {
        DEBUG('a', "Error: No se pudo crear el archivo de swap %s\n", swapName);
        ASSERT(false);
    }

    /// Abrimos el archivo
    swapFile = fileSystem->Open(swapName);
    ASSERT(swapFile != nullptr);
    
    DEBUG('a', "Archivo de SWAP creado: %s\n", swapName);

    /// Inicializamos la shadowTable
    shadowTable = new Bitmap(numPages);
    #endif

    /// Creamos la translationEntry
    pageTable = new TranslationEntry[numPages];
    for (unsigned i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
        pageTable[i].physicalPage = -1;
        pageTable[i].valid        = false; 
        pageTable[i].use          = false;
        pageTable[i].dirty        = false;
        pageTable[i].readOnly     = false;

    }

    /// Si no usuamos SWAP o DEMAND_LOADING, cargamos el ejecutable en memoria
    #if !defined(SWAP) && !defined(DEMAND_LOADING)
    /// Verificar que hay suficientes marcos libres
    ASSERT(numPages <= usedPages->CountClear());

    /// Cargamos las páginas
    for (unsigned i = 0; i < numPages; i++) LoadPage(i);

    /// Una vez cargado el archivo, lo puedo eliminar
    delete executableFile;

    #endif
}


/// Deallocate an address space.
AddressSpace::~AddressSpace()
{
    /// Destruimos la tabla de páginas
    if (pageTable != nullptr) {
        /// Antes de destruir la tabla, liberamos cada una de las entradas
        for (unsigned i = 0; i < numPages; i++) {
            if (pageTable[i].valid) { 

                #ifdef SWAP
                /// Si usamos SWAP, limpiamos las entradas del coremap
                coremap->Clear(pageTable[i].physicalPage);
                #else
                /// En caso contrario, limpiamos las entradas del bitmap
                usedPages->Clear(pageTable[i].physicalPage);
                #endif
            }
        }
        
        delete [] pageTable;
    }

    /// Si usamos SWAP o DEMAND_LOADING, borramos el ejecutable
    #if defined(SWAP) || defined(DEMAND_LOADING)
    delete executableFile;
    #endif

    /// Si está definido SWAP, destruimos el archivo SWAP.pid y la shadowTable
    #ifdef SWAP
    delete swapFile;
    delete shadowTable;
    #endif 

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
            pageTable[vpn].dirty |= tlb[i].dirty;
            pageTable[vpn].use   |= tlb[i].use;
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

TranslationEntry*
AddressSpace::GetPageTable() 
{ 
  return pageTable;
}

OpenFile*
AddressSpace::GetExecutableFile() 
{ 
  return executableFile;
}


unsigned 
AddressSpace::GetNumPages() const
{
  return numPages;
}

int 
AddressSpace::GetPid() 
{ 
  return pId;
}

#ifdef SWAP
Bitmap* 
AddressSpace::GetShadowTable() 
{ 
  return shadowTable;
}
#endif


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
        freePpn = coremap->PickVictim();

        /// Guardamos el marco físico en el archivo SWAP del proceso
        coremap->SavePage(freePpn);

        /// Limpiamos la entrada del coremap
        coremap->Clear(freePpn);
    }
    
    /// Obtenemos la dirección física del marco
    char *physAddr = &machine->mainMemory[freePpn * PAGE_SIZE];

    /// Limpiamos el marco físico
    memset(physAddr, 0, PAGE_SIZE);

    /// Si ya fue cargado alguna vez en memoria, cargo la página desde el archivo SWAP
    if (shadowTable->Test(vpn)) LoadFromSwapFile(vpn, freePpn, physAddr);
    
    /// En el caso contrario, lo cargo desde el ejecutable
    else LoadFromExecutable(vpn, freePpn, physAddr);
    
    /// Marcamos el coremap con la nueva página cargada
    coremap->Mark(freePpn, vpn, this);
    #else
    /// Buscamos un marco físico libre en el bitmap
    int freePpn = usedPages->Find();
    ASSERT(freePpn != -1);

    /// Obtenemos la dirección física del marco
    char *physAddr = &machine->mainMemory[freePpn * PAGE_SIZE];

    /// Limpiamos el marco físico asignado 
    memset(physAddr, 0, PAGE_SIZE);

    /// Una vez que el marco físico está limpio, cargamos en memoria RAM la página virtual
    LoadFromExecutable(vpn, freePpn, physAddr);
    #endif

    /// Marcamos los cambios en la tabla de páginas
    pageTable[vpn].valid = true;
    pageTable[vpn].dirty = false;
    pageTable[vpn].use = false;
    pageTable[vpn].physicalPage = freePpn;
}



#ifdef SWAP
void 
AddressSpace::LoadFromSwapFile(unsigned vpn, int ppn, char* ppAddr)
{
    ASSERT(swapFile != nullptr);
    int amountRead = swapFile->ReadAt(ppAddr, PAGE_SIZE, vpn * PAGE_SIZE);

    ASSERT(amountRead == PAGE_SIZE);

    stats->numSwapReads++;
    DEBUG('a', "LOADING FROM SWAPFILE: Página virtual %u cargada exitosamente en el marco físico %d.\n",
          vpn, ppn);
}
#endif

void 
AddressSpace::LoadFromExecutable(unsigned vpn, int ppn, char* ppAddr) 
{
    // Cargamos los datos desde el ejecutable si la página virtual intersecta con el código o datos inicializados
    Executable exe(executableFile);

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

    DEBUG('a', "LOADING FROM EXECUTABLE: Página virtual %u cargada exitosamente en el marco físico %d.\n",
          vpn, ppn);
}

#ifdef SWAP
void 
AddressSpace::WriteToSwap(unsigned vpn, char* physicalAddress)
{
    ASSERT(swapFile != nullptr);
    int amountWritten = swapFile->WriteAt(physicalAddress, PAGE_SIZE, vpn * PAGE_SIZE);
    ASSERT(amountWritten == PAGE_SIZE);
    stats->numSwapWrites++;
    DEBUG('a', "SWAP: Página virtual %u guardada en el archivo SWAP desde la memoria.\n", vpn);
    
    /// Marcamos la shadowTable
    shadowTable->Mark(vpn);
}
#endif








