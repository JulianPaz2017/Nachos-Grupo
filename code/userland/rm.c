#include "syscall.h"
#include "lib.h"

int
main(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: rm <filename>");
        Exit(-1);
    }

    if (Remove(argv[1]) < 0) {
        puts("Error: Could not remove file.");
        Exit(-1);
    }

    Exit(0);
}
