#include "console.hh"
#include "threads/system.hh"

#include <stdio.h>


static void
ConsoleWriteDone(void* arg) {
    ASSERT(arg != nullptr);
    SynchConsole *console = (SynchConsole *) arg;
    console->WriteDone();
}

static void
ConsoleCheckCharAvail(void* arg) {
    ASSERT(arg != nullptr);
    SynchConsole *console = (SynchConsole *) arg;
    console->CheckCharAvail();
}

SynchConsole::SynchConsole(const char *readFile, const char *writeFile)
{
  writersSemaphore = new Semaphore("SynchConsole writers semaphore", 0);
  readersSemaphore = new Semaphore("SynchConsole readers semaphore", 0);
  writersLock = new Lock("SynchConsole writers lock");
  readersLock = new Lock("SynchConsole readers lock");
  
  console = new Console(readFile, writeFile, ConsoleCheckCharAvail,
                        ConsoleWriteDone, this);
}

SynchConsole::~SynchConsole()
{
  delete console;
  delete writersSemaphore;
  delete readersSemaphore;
  delete writersLock;
  delete readersLock;
}

void
SynchConsole::CheckCharAvail()
{
  readersSemaphore->V();
}

/// Internal routine called when it is time to invoke the interrupt handler
/// to tell the Nachos kernel that the output character has completed.
void
SynchConsole::WriteDone()
{
  writersSemaphore->V(); 
}

/// Read a character from the input buffer, if there is any there.
/// Either return the character, or EOF if none buffered.
char
SynchConsole::GetChar()
{
  readersLock->Acquire();
  readersSemaphore->P();  // Esperar a que la interrupción avise que hay un char disponible
  char ch = console->GetChar();  // Recién ahora leer el carácter del buffer
  readersLock->Release();

  return ch;

}

/// Write a character to the simulated display, schedule an interrupt to
/// occur in the future, and return.
void
SynchConsole::PutChar(char ch)
{
  writersLock->Acquire();
  console->PutChar(ch);
  writersSemaphore->P();
  writersLock->Release();
}