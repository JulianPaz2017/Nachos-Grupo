#include "Channel.hh"
#include "system.hh"

Channel::Channel(const char *debugName) 
{
  name = debugName;
  lock = new Lock("Channel Lock");
  
  senderCV   = new Condition("Sender CV", lock);
  receiverCV = new Condition("Receiver CV", lock);
  ackCV      = new Condition("Ack CV", lock);

  waiters_send = 0;
  waiters_receive = 0;

  DEBUG('s', "Canal '%s' creado\n", name);
}

Channel::~Channel()
{
  delete senderCV;
  delete receiverCV;
  delete ackCV;
  delete lock;

  DEBUG('s', "Canal '%s' destruido\n", name);
}

const char*
Channel::GetName() const
{
  return name;
}

void
Channel::Send(int message)
{
  lock->Acquire();
  ASSERT(lock->IsHeldByCurrentThread());

  DEBUG('s', "Thread '%s' intenta enviar %d por el canal '%s'\n",
        currentThread->GetName(), message, name);

  // Si no hay nadie esperando para recibir, nos marcamos y esperamos.
  // Usamos un while por semántica de Mesa, aunque aquí el lock protege la secuencia.
  while (waiters_receive == 0) {
    DEBUG('s', "Thread '%s' se bloquea en Send (no hay receptores en '%s')\n", currentThread->GetName(), name);
    waiters_send++;
    senderCV->Wait();
    waiters_send--;
  }

  // Hay al menos un receptor listo. Colocamos el mensaje en el buffer.
  this->buffer = message;

  // Notificamos al receptor que el dato ya está listo en el buffer.
  receiverCV->Signal();

  // Esperamos el acuse de recibo para asegurar sincronismo (rendezvous).
  ackCV->Wait();

  DEBUG('s', "Thread '%s' terminó de enviar %d por el canal '%s'\n",
        currentThread->GetName(), message, name);

  lock->Release();
}


void 
Channel::Receive(int *message)
{
  ASSERT(message != nullptr);
  lock->Acquire();

  DEBUG('s', "Thread '%s' espera recibir por el canal '%s'\n",
        currentThread->GetName(), name);

  waiters_receive++;

  // Si ya hay un emisor esperando, lo despertamos para que proceda.
  if (waiters_send > 0) {
    senderCV->Signal();
  }

  // Esperamos a que un emisor ponga el dato y nos lo notifique.
  DEBUG('s', "Thread '%s' se bloquea en Receive (esperando emisor en '%s')\n",
        currentThread->GetName(), name);
  receiverCV->Wait();

  // Tomamos el dato del buffer.
  *message = this->buffer;

  // Enviamos el acuse de recibo al emisor.
  ackCV->Signal();

  waiters_receive--;

  DEBUG('s', "Thread '%s' recibió %d con éxito por el canal '%s'\n",
        currentThread->GetName(), *message, name);

  lock->Release();
}
