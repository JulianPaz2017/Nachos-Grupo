#include "syscall.h"
#include "lib.h"

int
main(int argc, char **argv)
{
    if (argc < 2) {
        Puts("Usage: rm <filename>");
        Exit(-1);
    }

    if (Remove(argv[1]) < 0) {
        Puts("Error: Could not remove file.");
        Exit(-1);
    }

    Exit(0);
}
