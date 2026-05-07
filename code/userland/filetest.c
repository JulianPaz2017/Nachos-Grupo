/// Simple program to test whether running a user program works.
///
/// Just do a “syscall” that shuts down the OS.
///
/// NOTE: for some reason, user programs with global data structures
/// sometimes have not worked in the Nachos environment.  So be careful out
/// there!  One option is to allocate data structures as automatics within a
/// procedure, but if you do this, you have to be careful to allocate a big
/// enough stack to hold the automatics!


#include "syscall.h"


int
main(void)
{
    Create("test.txt");
    OpenFileId o = Open("test.txt");

    Write("Hello world\n",12,o);
    Close(o);
    
    OpenFileId u = Open("test.txt");

    char buf[255];
    Read(buf,255,u);
    
    Write("Se leyó:",255,1);
    Write(buf,255,1);

    Close(u);
    Remove("test.txt");


    return 0;
}
