/// 

#ifndef NACHOS_SYNCH_CONSOLE___HH
#define NACHOS_SYNCH_CONSOLE___HH

#include "lib/utility.hh"
#include "machine/console.hh"
#include "threads/lock.hh"
#include "threads/semaphore.hh"


class SynchConsole {
public:
    /// Initialize the hardware  device.
    SynchConsole(const char *readFile, const char *writeFile);

    /// Clean up  emulation.
    ~SynchConsole();

    /// External interface -- Nachos kernel code can call these.

    /// Write `ch` to the  display, and return immediately.
    /// `writeHandler` is called when the I/O completes.
    void PutChar(char ch);

    /// Poll the  input.  If a char is available, return it.
    /// Otherwise, return EOF.  `readHandler` is called whenever there is a
    /// char to be gotten.
    char GetChar();

    // Internal emulation routines -- DO NOT call these.
    // Internal routines to signal I/O completion.

    void WriteDone();
    void CheckCharAvail();

  private:
    Console *console;  ///< Console.
    Semaphore *readersSemaphore;
    Semaphore *writersSemaphore;
    Lock *writersLock;
    Lock *readersLock;
};


#endif /* NACHOS_SYNCH_CONSOLE___HH */
