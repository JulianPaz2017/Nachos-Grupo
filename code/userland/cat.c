#include "syscall.h"
#include "lib.h"

#define BUFFER_SIZE 64

int
main(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: cat <filename>");
        Exit(-1);
    }

    OpenFileId file = Open(argv[1]);
    if (file < 0) {
        puts("Error: Could not open file.");
        Exit(-1);
    }

    char buffer[BUFFER_SIZE];
    int bytesRead;

    while ((bytesRead = Read(buffer, BUFFER_SIZE, file)) > 0) {
        Write(buffer, bytesRead, CONSOLE_OUTPUT);
    }

    Close(file);
    Exit(0);
}
