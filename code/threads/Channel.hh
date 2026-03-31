#ifndef NACHOS_THREADS_CHANNEL__HH
#define NACHOS_THREADS_CHANNEL__HH

#include "lock.hh"
#include "condition.hh"

class Channel {
public:
    Channel(const char *debugName, Lock *sendersLock, Lock *receiversLock);

    ~Channel();

    const char *GetName() const;

    void Send(int message);
    void Receive(int* message);

private:
    // Nombre del canal
    const char *name;

    // Variables de condición donde esperarán los emisores/receptores respectivamente
    static Condition *senders;
    static Condition *receivers;

    // Lock para proteger el canal
    Lock *channelLock;

    // mensaje intermedio 
    int mensaje;

    // Variables enteras para las variables de condición
    int number_of_senders;
    int number_of_receivers;
};


#endif
