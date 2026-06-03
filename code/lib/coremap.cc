#include "coremap.hh"
#include <time.h>
#include <stdlib.h>

#ifdef USER_PROGRAM
#include "threads/system.hh"
#include "userprog/address_space.hh"
#endif

// Incluidos para el Clock
#include "mmu.hh"
#include "../threads/thread.hh"
#include "../userprog/address_space.hh"


Coremap::Coremap(unsigned nitems){

  ASSERT(nitems > 0);

  // Asignamos el tamaño del coremap
  sizeCoremap = nitems;

  // Creamos el coremap
  coremap = new CoremapEntry [nitems];

  clockHand = 0;
}

Coremap::~Coremap() {
  // Destruimos el coremap
  delete [] coremap;
}

void 
Coremap::Mark(unsigned pfn, int vpn, int pid) {
  ASSERT(pfn < sizeCoremap);
  coremap[pfn].Set(vpn, pid);
}

void 
Coremap::Clear(unsigned pfn) {
  ASSERT(pfn < sizeCoremap);
  coremap[pfn].Clear();
}

bool 
Coremap::Used(unsigned pfn) const {
  ASSERT(pfn < sizeCoremap);
  return coremap[pfn].IsInUse(); 
}

int
Coremap::Find() {
  for (int i = 0; i < sizeCoremap; i++) {
    if (!(coremap[i].IsInUse())) return i;
  }

  return -1;
}

unsigned 
Coremap::CountClear() const {
  int clearPages = 0;

  for (int i = 0; i < sizeCoremap; i++) {
    if (!(coremap[i].IsInUse())) clearPages++;
  }

  return clearPages;
}

void 
Coremap::Print() const {
  for (int i = 0; i < sizeCoremap; i++) {
    printf("Marco %2u: ", i);
    coremap[i].Print();
  }
}

#if defined(USE_TLB) && defined(USER_PROGRAM)
void
Coremap::SyncTLBEntry(int pid, int vpn)
{


    if (pid != (int) currentThread->GetPid())
        return;

    TranslationEntry *tlb = machine->GetMMU()->tlb;

    Thread *thread = processTable->Get(pid);
    ASSERT(thread != nullptr);

    TranslationEntry *pageTable =
        thread->space->GetPageTable();

    for (unsigned i = 0; i < TLB_SIZE; i++) {

        if (tlb[i].valid &&
            tlb[i].virtualPage == (unsigned) vpn) {

            pageTable[vpn].use   |= tlb[i].use;
            pageTable[vpn].dirty |= tlb[i].dirty;

            break;
        }
    }
  }
#endif


#ifdef USER_PROGRAM

TranslationEntry &
Coremap::GetEntry(unsigned frame)
{

    ASSERT(frame < sizeCoremap);

    int pid = coremap[frame].GetProcessId();
    int vpn = coremap[frame].GetVirtualPage();

  #ifdef USE_TLB
    SyncTLBEntry(pid, vpn);
  #endif

    Thread *thread = processTable->Get(pid);
    ASSERT(thread != nullptr);

    AddressSpace *space = thread->space;
    ASSERT(space != nullptr);

    return space->GetPageTable()[vpn];
}
#endif


int \
Coremap::PickVictim() {
  #if defined(PRPOLICY_CLOCK)

    int candidate = -1;

    // Primera pasada:
    // buscamos (0,0) y recordamos el primer (0,1)
    for (unsigned n = 0; n < sizeCoremap; n++) {

        unsigned i = (clockHand + n) % sizeCoremap;

        TranslationEntry &entry = GetEntry(i);

        // Clase (0,0)
        if (!entry.use && !entry.dirty) {
            clockHand = (i + 1) % sizeCoremap;
            return i;
        }

        // Clase (0,1)
        if (!entry.use && entry.dirty && candidate == -1) {
            candidate = i;
        }
    }

    // Segunda pasada:
    // limpiamos use
    for (unsigned n = 0; n < sizeCoremap; n++) {

        unsigned i = (clockHand + n) % sizeCoremap;

        TranslationEntry &entry = GetEntry(i);

        entry.use = false;

    }

    // Si encontramos una página (0,1), la usamos
    if (candidate != -1) {
        clockHand = (candidate + 1) % sizeCoremap;
        return candidate;
    }

    // Caso extremo: todas eran (1,x).
    // Después de limpiar use, elegimos la primera.
    unsigned victim = clockHand;
    clockHand = (clockHand + 1) % sizeCoremap;
    return victim;

  #elif defined(PRPOLICY_FIFO)

    unsigned victim = 0;
    unsigned oldest = coremap[0].GetPageTime();

    for (unsigned i = 1; i < sizeCoremap; i++) {
        if (coremap[i].GetPageTime() < oldest) {
            oldest = coremap[i].GetPageTime();
            victim = i;
        }
    }

    return victim;
  #else
    /// Política random
    return rand() % sizeCoremap;
  #endif
}

void 
Coremap::SavePage(unsigned ppn) {
#if defined(USER_PROGRAM) && defined(SWAP)
    ASSERT(ppn < sizeCoremap);
    if (!coremap[ppn].IsInUse()) {
        return;
    }

    int pid = coremap[ppn].GetProcessId();
    int vpn = coremap[ppn].GetVirtualPage();

    Thread *thread = processTable->Get(pid);
    ASSERT(thread != nullptr);
    AddressSpace *space = thread->space;
    ASSERT(space != nullptr);

    TranslationEntry *pageTable = space->GetPageTable();
    TranslationEntry &entry = pageTable[vpn];

#ifdef USE_TLB
    if (pid == (int)currentThread->GetPid()) {
        TranslationEntry *tlb = machine->GetMMU()->tlb;
        for (unsigned i = 0; i < TLB_SIZE; i++) {
            if (tlb[i].valid && tlb[i].virtualPage == (unsigned)vpn) {
                entry.dirty = tlb[i].dirty;
                entry.use = tlb[i].use;
                tlb[i].valid = false;
                break;
            }
        }
    }
#endif

    char *physAddr = &machine->mainMemory[ppn * PAGE_SIZE];
    space->WriteToSwap(vpn, physAddr);

    entry.physicalPage = -1;
    entry.valid = false;
    entry.dirty = false;
    entry.use = false;
#endif
}