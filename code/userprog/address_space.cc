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
#include <stdint.h>


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

    DEBUG('a', "Initializing address space, num pages %u, size %u\n",
          numPages, size);

#ifdef DEMAND_LOADING
    // ── Carga por demanda ────────────────────────────────────────────────
    // No reservamos marcos físicos ni cargamos ningún byte del ejecutable
    // aquí.  Toda la carga ocurre en LoadPage(), invocado desde el
    // PageFaultHandler al primer acceso a cada página.
    //
    // Tampoco reservamos páginas para la pila: también se cargarán al
    // primer acceso (PageFaultHandler → LoadPage → SEG_STACK).

    // Guardamos el archivo abierto para poder leer fragmentos del ejecutable
    // más adelante.
    executableFile = executable_file;

    // Metadatos por página: tipo de segmento y offset dentro del segmento.
    pageSegments = new PageSegment[numPages];
    pageOffsets  = new uint32_t[numPages];

    // Construimos la tabla de páginas con todas las entradas inválidas.
    pageTable = new TranslationEntry[numPages];
    for (unsigned i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
        pageTable[i].physicalPage = 0;     // Irrelevante hasta que se cargue.
        pageTable[i].valid        = false; // Sin marco físico aún.
        pageTable[i].use          = false;
        pageTable[i].dirty        = false;
        pageTable[i].readOnly     = false;
        // Por defecto asumimos pila/bss (ceros).
        pageSegments[i] = SEG_STACK;
        pageOffsets[i]  = 0;
    }

    // Anotar a qué segmento pertenece cada página virtual.
    // Una página puede cubrir parte de CODE y parte de INIT_DATA si el
    // segmento no está alineado a PAGE_SIZE; manejamos eso de forma
    // conservadora: la primera mitad gana (el resto es correcto porque
    // LoadPage lee página completa del segmento correcto).
    // En la práctica coff2noff alinea los segmentos a páginas.

    uint32_t codeSize     = exe.GetCodeSize();
    uint32_t codeAddr     = exe.GetCodeAddr();
    uint32_t initDataSize = exe.GetInitDataSize();
    uint32_t initDataAddr = exe.GetInitDataAddr();

    // Marcar páginas de código.
    for (uint32_t off = 0; off < codeSize; off += PAGE_SIZE) {
        unsigned vpn = (codeAddr + off) / PAGE_SIZE;
        if (vpn < numPages) {
            pageSegments[vpn] = SEG_CODE;
            pageOffsets[vpn]  = (codeAddr + off) - (uint32_t)(vpn * PAGE_SIZE);
            // off dentro del segmento correspondiente a la primera byte de vpn:
            // Como codeAddr+off puede no ser el inicio exacto del vpn,
            // calculamos el offset del segmento que coincide con vpn*PAGE_SIZE.
            // Para simplificar, guardamos el byte offset del segmento de código
            // que corresponde al inicio de esta página virtual.
            uint32_t pageStart = (uint32_t)(vpn * PAGE_SIZE);
            pageOffsets[vpn] = (pageStart >= codeAddr)
                                ? (pageStart - codeAddr)
                                : 0;
        }
    }

    // Marcar páginas de datos inicializados.
    for (uint32_t off = 0; off < initDataSize; off += PAGE_SIZE) {
        unsigned vpn = (initDataAddr + off) / PAGE_SIZE;
        if (vpn < numPages && pageSegments[vpn] != SEG_CODE) {
            pageSegments[vpn] = SEG_INIT_DATA;
            uint32_t pageStart = (uint32_t)(vpn * PAGE_SIZE);
            pageOffsets[vpn] = (pageStart >= initDataAddr)
                                ? (pageStart - initDataAddr)
                                : 0;
        }
    }
    // Las páginas de bss/uninit y pila ya quedaron marcadas SEG_STACK
    // (ceros) por defecto.

#else
    // ── Carga eagerly (comportamiento original) ──────────────────────────
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
#ifdef DEMAND_LOADING
    // Solo liberamos los marcos que realmente fueron asignados.
    for (unsigned i = 0; i < numPages; i++) {
        if (pageTable[i].valid) {
            usedPages->Clear(pageTable[i].physicalPage);
        }
    }
    delete [] pageSegments;
    delete [] pageOffsets;
    // Cerramos el archivo ejecutable (ya no se necesita).
    delete executableFile;
#else
    for (unsigned i = 0; i < numPages; i++) {
        usedPages->Clear(pageTable[i].physicalPage);
    }
#endif
    delete [] pageTable;
}

#ifdef DEMAND_LOADING
/// Carga la página virtual `vpn` en un marco físico libre.
///
/// Esta función es invocada desde el PageFaultHandler cuando se detecta
/// que la entrada de la pageTable tiene `valid = false`.
/// Asigna un marco libre con `usedPages->Find()`, lo inicializa a cero,
/// y lo rellena con el contenido del segmento correspondiente del ejecutable.
void
AddressSpace::LoadPage(unsigned vpn)
{
    ASSERT(vpn < numPages);
    ASSERT(!pageTable[vpn].valid);

    // 1. Obtener un marco físico libre.
    int physPage = usedPages->Find();
    ASSERT(physPage != -1);  // No debe fallar si usamos -m adecuado.

    // 2. Limpiar el marco (garantiza que BSS/pila estén en cero).
    char *frameBase = &machine->mainMemory[physPage * PAGE_SIZE];
    memset(frameBase, 0, PAGE_SIZE);

    // 3. Copiar contenido desde el ejecutable si corresponde.
    Executable exe(executableFile);
    exe.CheckMagic();

    switch (pageSegments[vpn]) {
        case SEG_CODE: {
            // Calcular cuántos bytes del segmento de código caben en esta página.
            uint32_t segOffset = pageOffsets[vpn];
            uint32_t codeSize  = exe.GetCodeSize();
            uint32_t codeAddr  = exe.GetCodeAddr();

            // Inicio de la página virtual en bytes.
            uint32_t pageVA    = (uint32_t)(vpn * PAGE_SIZE);
            // Posición dentro del segmento de código donde comienza esta página.
            uint32_t startInSeg = (pageVA >= codeAddr) ? (pageVA - codeAddr) : 0;
            (void) segOffset;  // pageOffsets ya está calculado pero usamos startInSeg

            uint32_t available = (startInSeg < codeSize) ? (codeSize - startInSeg) : 0;
            uint32_t toCopy    = (available < PAGE_SIZE) ? available : PAGE_SIZE;

            if (toCopy > 0) {
                exe.ReadCodeBlock(frameBase, toCopy, startInSeg);
            }
            // El segmento de código es de solo lectura en las páginas puras.
            // No marcamos readOnly aquí para simplificar (MIPS no distingue
            // ejecución vs. lectura en NachOS simulado).
            DEBUG('a', "LoadPage: VPN=%u <- SEG_CODE offset=%u toCopy=%u\n",
                  vpn, startInSeg, toCopy);
            break;
        }

        case SEG_INIT_DATA: {
            uint32_t initDataSize = exe.GetInitDataSize();
            uint32_t initDataAddr = exe.GetInitDataAddr();

            uint32_t pageVA     = (uint32_t)(vpn * PAGE_SIZE);
            uint32_t startInSeg = (pageVA >= initDataAddr) ? (pageVA - initDataAddr) : 0;

            uint32_t available = (startInSeg < initDataSize) ? (initDataSize - startInSeg) : 0;
            uint32_t toCopy    = (available < PAGE_SIZE) ? available : PAGE_SIZE;

            if (toCopy > 0) {
                exe.ReadDataBlock(frameBase, toCopy, startInSeg);
            }
            DEBUG('a', "LoadPage: VPN=%u <- SEG_INIT_DATA offset=%u toCopy=%u\n",
                  vpn, startInSeg, toCopy);
            break;
        }

        case SEG_UNINIT:
        case SEG_STACK:
            // Ya limpiado con memset arriba: nada más que hacer.
            DEBUG('a', "LoadPage: VPN=%u <- SEG_ZERO (stack/bss)\n", vpn);
            break;
    }

    // 4. Actualizar la entrada de la pageTable.
    pageTable[vpn].physicalPage = (unsigned) physPage;
    pageTable[vpn].valid        = true;
    pageTable[vpn].use          = false;
    pageTable[vpn].dirty        = false;
    pageTable[vpn].readOnly     = false;
}
#endif  // DEMAND_LOADING

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
