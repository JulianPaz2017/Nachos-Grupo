#ifndef NACHOS_THREADS_CHANNEL__HH
#define NACHOS_THREADS_CHANNEL__HH

#include "lock.hh"
#include "condition.hh"

class Channel {
public:
    Channel(const char *debugName);

    ~Channel();

    const char *GetName() const;

    void Send(int message);
    void Receive(int* message);

private:
    const char *name;

    // Lock único para proteger todo el estado del canal
    Lock *lock;

    // Variables de condición para sincronización
    Condition *senderCV;
    Condition *receiverCV;
    Condition *ackCV;

    // Buffer intermedio para la transferencia del dato
    int buffer;

    // Contadores de hilos esperando
    int waiters_send;
    int waiters_receive;
};


#endif
