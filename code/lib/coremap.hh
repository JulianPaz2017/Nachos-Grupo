#ifndef NACHOS_LIB_COREMAP__HH
#define NACHOS_LIB_COREMAP__HH

#include "utility.hh"
#include "coremap_entry.hh"

#include "system.hh"
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

    void SyncTLBEntry(int pid, int vpn);
    TranslationEntry &GetEntry(unsigned frame);
    

private:
    unsigned sizeCoremap;

    unsigned clockHand; // Para elegir pagina victima

//#ifdef USER_PROGRAM
//    void SyncTLBEntry(int pid, int vpn);
//    TranslationEntry &GetEntry(unsigned frame);
//#endif

    CoremapEntry *coremap;
};


#endif
