#ifndef NACHOS_MACHINE_COREMAPENTRY__HH
#define NACHOS_MACHINE_COREMAPENTRY__HH


#include "lib/utility.hh"
#include <unistd.h>
#include <stdio.h>

class AddressSpace;

class CoremapEntry {
public:
    /// Crea una nueva entrada de la coremap
    CoremapEntry() { Clear(); }

    /// Inicializa una entrada de la coremap
    void Clear() {
        this->inUse = false;
        this->virtualPage = -1;
        this->addressSpace = nullptr;
        this->isLocked = false;

        #ifdef PRPOLICY_FIFO
        this->pageTime = 0;
        #endif
    }

    /// Asocia el marco a un proceso y página virtual
    void Set(int vpn, AddressSpace* addrSpace) {
        ASSERT(vpn >= 0);
        ASSERT(addrSpace != nullptr);

        inUse = true;
        isLocked = false;
        virtualPage = vpn;
        addressSpace = addrSpace;

        #ifdef PRPOLICY_FIFO
            pageTime = stats->totalTicks; 
        #endif
    }

    /// Verifica si la página está en uso o no
    bool IsInUse() { return inUse; }

    /// Verifica si la página está bloqueada o no
    bool IsLocked() { return isLocked; }
    
    /// Getter para el número de página virtual
    int GetVirtualPage() { return virtualPage; }

    /// Getter del espacio de direcciones
    AddressSpace* GetAddressSpace() { return addressSpace; }
    
    /// Indica que el marco físico está siendo utilizado
    /// con propósitos de I/O
    void Lock() { isLocked = true; }

    /// Indica que el marco físico está dejó de ser utilizado
    /// con propósitos de I/O
    void Unlock() { isLocked = false; }

    #ifdef PRPOLICY_FIFO
    /// Devuelve el instante de tiempo en el que se cargó la página
    unsigned int GetPageTime() { return pageTime; }
    #endif

    /// Imprime una entrada del coremap
    void Print() {
        if (!inUse) printf("[   LIBRE   ]\n");

        else {
            printf("[  OCUPADO  ] VPN: %3d", virtualPage);
            #ifdef PRPOLICY_FIFO
            printf(" | Tick: %7u", pageTime);
            #endif
            printf("\n");
        }
    }

private:
    /// Representa si el marco físico está ocupado
    bool inUse;

    /// Representa si el marco físico está siendo utilizado para
    /// un propósito de I/O
    bool isLocked;

    /// Número página virtual asociada al marco físico
    int virtualPage;

    /// Espacio de direcciones dueño de la página virtual
    AddressSpace *addressSpace;

    #ifdef PRPOLICY_FIFO
    /// Instante de tiempo en el que se ocupó el marco físico
    unsigned int pageTime;
    #endif
};

#endif