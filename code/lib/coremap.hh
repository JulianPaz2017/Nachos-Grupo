#ifndef NACHOS_LIB_COREMAP__HH
#define NACHOS_LIB_COREMAP__HH

#include "utility.hh"
#include "coremap_entry.hh"

class Coremap {
public:
    Coremap(unsigned nitems);

    ~Coremap();

    void Mark(unsigned pfn);

    void Clear(unsigned pfn);

    bool Used(unsigned pfn) const;

    int Find();

    unsigned CountClear() const;

    void Print() const;

    int PickVictim() {return 0;};
private:
    unsigned sizeCoremap;

    CoremapEntry *coremap;
};


#endif
