#ifndef NACHOS_LIB_COREMAP__HH
#define NACHOS_LIB_COREMAP__HH

#include "utility.hh"
#include "coremap_entry.hh"

class Coremap {
public:
    Coremap(unsigned nitems);

    ~Coremap();

    void Mark(unsigned pfn, int vpn, int pid);

    void Clear(unsigned pfn);

    bool Used(unsigned pfn) const;

    int Find();

    unsigned CountClear() const;

    void Print() const;

    int PickVictim();

    void SavePage(unsigned ppn);
private:
    unsigned sizeCoremap;

    CoremapEntry *coremap;
};


#endif
