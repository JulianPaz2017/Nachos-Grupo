#ifndef NACHOS_LIB_COREMAP__HH
#define NACHOS_LIB_COREMAP__HH

#ifdef SWAP
#include "utility.hh"
#include "coremap_entry.hh"

class AddressSpace;

class Coremap {
public:
    /// Crea un coremap de 'nFrames' marcos físicos
    Coremap(unsigned nFrames);

    /// Destruye un coremap
    ~Coremap();

    /// Marca un marco físico a través de un número de 
    /// página virtual y un espacio de direcciones
    void Mark(unsigned ppn, int vpn, AddressSpace* addrSpace);

    /// Limpia una entrada del coremap
    void Clear(unsigned ppn);

    /// Verifica si una entrada está en uso
    bool IsUsed(unsigned ppn) const;

    /// Verifica si una entrada está bloqueada
    bool IsLocked(unsigned ppn) const;

    /// Devuelve un número de marco físico libre o -1 en caso contrario
    int Find();

    /// Cuenta la cantidad de páginas libres
    unsigned CountClear() const;

    /// Imprime el coremap
    void Print() const;

    /// Selecciona un marco físico para realizar SWAP
    int PickVictim();

    /// Guarda una página virtual (la asociada al marco físico ingresado) 
    /// en el disco
    void SavePage(unsigned ppn);

private:
    /// Tamaño del coremap
    unsigned sizeCoremap;

    /// Arreglo de entradas del coremap
    CoremapEntry *coremap;

    #ifdef PRPOLICY_CLOCK
    /// Manecilla del reloj para el algoritmo de reemplazo Clock
    unsigned clockHand;
    #endif
};


#endif
#endif