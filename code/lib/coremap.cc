#include "coremap.hh"
#include <time.h>
#include <stdlib.h>

#ifdef USER_PROGRAM
#include "threads/system.hh"
#include "userprog/address_space.hh"
#endif

Coremap::Coremap(unsigned nitems){

  ASSERT(nitems > 0);

  // Asignamos el tamaño del coremap
  sizeCoremap = nitems;

  // Creamos el coremap
  coremap = new CoremapEntry [nitems];
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

int \
Coremap::PickVictim() {
  #if defined(PRPOLICY_CLOCK)
    return 0;
  #elif defined(PRPOLICY_FIFO)
    return 0;
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