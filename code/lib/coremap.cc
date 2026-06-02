#include "coremap.hh"

Coremap::Coremap(unsigned nitems){

  ASSERT(nitems > 0);

  // Asignamos el tamaño del coremap
  sizeCoremap = nitems;

  // Creamos el coremap
  coremap = new CoremapEntry [numWords];
}

Coremap::~Coremap() {
  // Destruimos el coremap
  delete [] coremap;
}

void 
Coremap::Mark(unsigned pfn, int vpn, int pid) {
  ASSERT(pfn < sizeCoremap);
  coremap[pfn]->Set(vpn, pid);
}

void 
Coremap::Clear(unsigned pfn) {
  ASSERT(pfn < sizeCoremap);
  coremap[pfn]->Clear();
}

bool 
Coremap::Used(unsigned pfn) const {
  ASSERT(pfn < sizeCoremap);
  return coremap[pfn]->IsInUse(); 
}

int
Coremap::Find() {
  for (int i = 0; i < sizeCoremap; i++) {
    if (!(coremap[i]->IsInUse())) return i;
  }

  return -1;
}

unsigned 
Coremap::CountClear() const {
  int clearPages = 0;

  for (int i = 0; i < sizeCoremap; i++) {
    if (!(coremap[i]->IsInUse())) clearPages++;
  }

  return clearPages;
}

void 
Coremap::Print() const {
  for (int i = 0; i < sizeCoremap; i++) {
    printf("Marco %2u: ", i);
    coremap[i]->Print();
  }
}