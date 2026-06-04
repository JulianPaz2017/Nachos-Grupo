#ifdef SWAP
#include "threads/system.hh"
#include "coremap.hh"
#include "userprog/address_space.hh"
#include "machine/machine.hh"
#include "machine/translation_entry.hh"
#include <time.h>
#include <stdlib.h>

Coremap::Coremap(unsigned nFrames)
{
  ASSERT(nFrames > 0);

  sizeCoremap = nFrames;
  coremap = new CoremapEntry [nFrames];

  #ifdef PRPOLICY_CLOCK
  clockHand = 0;
  #endif
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
    /// Política CLOCK:
    /// Prioridad de víctima según (use, dirty):
    ///   Clase 1: use=0, dirty=0  -> mejor víctima
    ///   Clase 2: use=0, dirty=1
    ///   Clase 3: use=1, dirty=0  -> resetea use (segunda oportunidad)
    ///   Clase 4: use=1, dirty=1  -> peor víctima, resetea use
    ///
    /// Se hacen hasta 4 pasadas circulares partiendo desde clockHand.
    /// En cada pasada se busca la primera página de la clase actual;
    /// si no se encuentra se avanza a la siguiente clase.
    /// En las pasadas 3 y 4 se resetea el bit use para dar segunda oportunidad.
    for (unsigned pass = 0; pass < 4; pass++) {
      bool resetUse = (pass >= 2);

      for (unsigned i = 0; i < sizeCoremap; i++) {
        unsigned ppn = (clockHand + i) % sizeCoremap;

        if (!coremap[ppn].IsInUse()) continue;

        /// Leemos los bits use y dirty de la tabla de páginas del proceso dueño.
        /// Si el proceso es el actual y usa TLB, sincronizamos primero.
        AddressSpace *addrSpace = coremap[ppn].GetAddressSpace();
        int vpn = coremap[ppn].GetVirtualPage();
        TranslationEntry *pageTable = addrSpace->GetPageTable();

        #ifdef USE_TLB
        if (addrSpace->GetPid() == currentThread->GetPid()) {
          TranslationEntry *tlb = machine->GetMMU()->tlb;
          for (unsigned t = 0; t < TLB_SIZE; t++) {
            if (tlb[t].valid && tlb[t].virtualPage == (unsigned)vpn) {
              pageTable[vpn].use   |= tlb[t].use;
              pageTable[vpn].dirty |= tlb[t].dirty;
              break;
            }
          }
        }
        #endif

        bool usebit   = pageTable[vpn].use;
        bool dirtybit = pageTable[vpn].dirty;

        bool isVictim = false;
        switch (pass) {
          case 0: isVictim = (!usebit && !dirtybit); break;  // clase 1
          case 1: isVictim = (!usebit &&  dirtybit); break;  // clase 2
          case 2: isVictim = ( usebit && !dirtybit); break;  // clase 3
          case 3: isVictim = ( usebit &&  dirtybit); break;  // clase 4
        }

        if (resetUse) {
          /// Damos segunda oportunidad: limpiamos el bit use
          pageTable[vpn].use = false;
          #ifdef USE_TLB
          if (addrSpace->GetPid() == currentThread->GetPid()) {
            TranslationEntry *tlb = machine->GetMMU()->tlb;
            for (unsigned t = 0; t < TLB_SIZE; t++) {
              if (tlb[t].valid && tlb[t].virtualPage == (unsigned)vpn) {
                tlb[t].use = false;
                break;
              }
            }
          }
          #endif
        }

        if (isVictim) {
          clockHand = (ppn + 1) % sizeCoremap;
          return ppn;
        }
      }
    }

    /// Fallback: nunca debería llegar aquí si el coremap tiene al menos
    /// una página en uso y no bloqueada.
    return clockHand % sizeCoremap;

  #elif defined(PRPOLICY_FIFO)
    /// Política FIFO: Seleccionamos la entrada más vieja
    unsigned victim = 0;
    unsigned long oldest = coremap[0].GetPageTime();

    for (unsigned i = 1; i < sizeCoremap; i++) {
      if (coremap[i].GetPageTime() < oldest) {
        oldest = coremap[i].GetPageTime();
        victim = i;
      }
    }

    DEBUG('a', "FIFO POLICY\n");
    return victim;
  #else
  /// Política RANDOM: Seleccionamos al azar
  DEBUG('a', "RANDOM POLICY\n");
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