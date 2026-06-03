#ifndef NACHOS_MACHINE_COREMAPENTRY__HH
#define NACHOS_MACHINE_COREMAPENTRY__HH


#include "lib/utility.hh"
#include <unistd.h>
#include <stdio.h>


class CoremapEntry {
public:
    /// Por defecto llamamos a clear en el constructor
    CoremapEntry() { Clear(); }

    /// Inicializa o libera el marco
    void Clear() {
        use = false;
        virtualPage = -1;
        processId = -1;
#ifdef PRPOLICY_FIFO
        pageTime = 0;
#endif
    }

    /// Asocia el marco a un proceso y página virtual
    void Set(int vpn, int pid) {
        use = true;
        virtualPage = vpn;
        processId = pid;

#ifdef PRPOLICY_FIFO
        pageTime = stats->totalTicks; 
#endif
    }

    /// Corrobora si el marco está en uso
    bool IsInUse() const { return use; }

    bool Match(int vpn, int pid) const {
        return use && (virtualPage == vpn) && (processId == pid);
    }
    
    /// Getter para el PID
    int GetProcessId() const { return processId; }
    
    /// Getter para la VPN
    int GetVirtualPage() const { return virtualPage; }

    /// Imprime una entrade del coremap
    void Print() const {        
        if (!use) {
            printf("[   LIBRE   ]\n");
        } else {
            printf("[  OCUPADO  ] PID: %3d | VPN: %3d", processId, virtualPage);
#ifdef PRPOLICY_FIFO
            printf(" | Tick: %7u", pageTime);
#endif
            printf("\n");
        }
    }

#ifdef PRPOLICY_FIFO
    unsigned int GetPageTime() const { return pageTime; }
#endif


private:
   bool use;
   int virtualPage;
   int processId;
#ifdef PRPOLICY_FIFO
   unsigned int pageTime;
#endif
};

#endif