#include "syscall.h"
#include "lib.h"

#define BUFFER_SIZE 64

int
main(int argc, char **argv)
{
    if (argc < 2) {
        Puts("Usage: cat <filename>");
        Exit(-1);
    }

    OpenFileId file = Open(argv[1]);
    if (file < 0) {
        Puts("Error: Could not open file.");
        Exit(-1);
    }

    char buffer[BUFFER_SIZE];
    int bytesRead;

    do {
        bytesRead = Read(buffer, BUFFER_SIZE, file);

        if (bytesRead > 0) {
            Write(buffer, bytesRead, CONSOLE_OUTPUT);
        }
    }while (bytesRead > 0);

    Close(file);
    Exit(0);
}
