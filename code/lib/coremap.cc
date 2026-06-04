#ifdef SWAP
#include "coremap.hh"
#include "userprog/address_space.hh"
#include "threads/system.hh"
#include "machine/machine.hh"
#include <time.h>
#include <stdlib.h>

Coremap::Coremap(unsigned nFrames)
{
  ASSERT(nFrames > 0);

  sizeCoremap = nFrames;
  coremap = new CoremapEntry [nFrames];
}

Coremap::~Coremap() 
{
  delete [] coremap;
}

void 
Coremap::Mark(unsigned ppn, int vpn, AddressSpace *addrSpace) 
{
  ASSERT(ppn < sizeCoremap);
  coremap[ppn].Set(vpn, addrSpace);
}

void 
Coremap::Clear(unsigned ppn) 
{
  ASSERT(ppn < sizeCoremap);
  coremap[ppn].Clear();
}

bool 
Coremap::IsUsed(unsigned ppn) const 
{
  ASSERT(ppn < sizeCoremap);
  return coremap[ppn].IsInUse(); 
}

bool 
Coremap::IsLocked(unsigned ppn) const 
{
  ASSERT(ppn < sizeCoremap);
  return coremap[ppn].IsLocked(); 
}

int
Coremap::Find() 
{
  for (int i = 0; i < sizeCoremap; i++) {
    if (!(coremap[i].IsInUse())) return i;
  }

  return -1;
}

unsigned 
Coremap::CountClear() const 
{
  int clearPages = 0;

  for (int i = 0; i < sizeCoremap; i++) {
    if (!(coremap[i].IsInUse())) clearPages++;
  }

  return clearPages;
}

void 
Coremap::Print() const 
{
  for (int i = 0; i < sizeCoremap; i++) {
    printf("Marco %2u: ", i);
    coremap[i].Print();
  }
}

int \
Coremap::PickVictim() 
{
  #if defined(PRPOLICY_CLOCK)
    /// Política CLOCK: ---
    return 0;
  #elif defined(PRPOLICY_FIFO)
    /// Política FIFO: Seleccionamos la entrada más vieja
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
    /// Política RANDOM: Seleccionamos al azar
    return rand() % sizeCoremap;
  #endif
}

void
Coremap::SavePage(unsigned ppn) 
{
  ASSERT(ppn < sizeCoremap);

  /// Si la página no está en uso, finalizamos
  if (!coremap[ppn].IsInUse()) return;

  /// Obtenemos el espacio de direcciones y el número de 
  /// página virtual asociado al marco físico
  int vpn = coremap[ppn].GetVirtualPage();
  AddressSpace *addrSpace = coremap[ppn].GetAddressSpace();

  ASSERT(addrSpace != nullptr);

  /// Obtenemos la tabla de páginas
  TranslationEntry *pageTable = addrSpace->GetPageTable();
  
  /// Debemos modificar la entrada en la TLB
  #ifdef USE_TLB
  if (addrSpace->GetPid() == currentThread->GetPid()) {
    TranslationEntry *tlb = machine->GetMMU()->tlb;
    
    for (unsigned i = 0; i < TLB_SIZE; i++) {
      if (tlb[i].valid && tlb[i].virtualPage == (unsigned)vpn) {
        pageTable[vpn].dirty |= tlb[i].dirty;
        pageTable[vpn].use |= tlb[i].use;
        tlb[i].valid = false;
        break;
      }
    }
  }
  #endif
  
  /// Obtenemos la dirección de memoria física donde inicia
  /// el marco físico
  char *physAddr = &machine->mainMemory[ppn * PAGE_SIZE];
  
  /// Escribimos el marco físico en el archivo SWAP.pid
  if (pageTable[vpn].dirty) {
    addrSpace->WriteToSwap(vpn, physAddr);
  }
  
  /// Modificamos la tabla de páginas del proceso y el coremap
  pageTable[vpn].physicalPage = -1;
  pageTable[vpn].valid = false;
  pageTable[vpn].dirty = false;
  pageTable[vpn].use = false;

  this->Clear(ppn);
}
#endif