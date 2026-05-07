#include "syscall.h"
#include "lib.h"

#define BUFFER_SIZE 64

int
main(int argc, char **argv)
{
    if (argc < 3) {
        puts("Usage: cp <source> <destination>");
        Exit(-1);
    }

    OpenFileId src = Open(argv[1]);
    if (src < 0) {
        puts("Error: Could not open source file.");
        Exit(-1);
    }

    // Intentamos crear el archivo destino por si no existe
    Create(argv[2]);

    OpenFileId dst = Open(argv[2]);
    if (dst < 0) {
        puts("Error: Could not open destination file.");
        Close(src);
        Exit(-1);
    }

    char buffer[BUFFER_SIZE];
    int bytesRead;

    while ((bytesRead = Read(buffer, BUFFER_SIZE, src)) > 0) {
        Write(buffer, bytesRead, dst);
    }

    Close(src);
    Close(dst);
    Exit(0);
}
