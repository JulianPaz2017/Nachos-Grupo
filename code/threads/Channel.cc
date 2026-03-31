#include "Channel.hh"

Channel::Channel(const char *debugName, Lock *sendersLock, Lock *receiversLock) 
{
  // Verificamos que ninguno de los locks sea nulo
  ASSERT(sendersLock && receiversLock);
  
  number_of_senders   = 0;
  number_of_receivers = 0;

  channelLock = new Lock("Channel lock");

  senders   = new Condition("Senders lock", sendersLock);
  receivers = new Condition("Receivers lock", receiversLock);

  name = debugName;

  DEBUG('s', "Canal'%s' creada (lock asociado: '%s')\n", name, 
    channelLock->GetName());
}

Channel::~Channel()
{
  ASSERT((number_of_receivers > 0) || (number_of_senders > 0));

  delete senders;
  delete receivers;

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
  
}

