// tlb_dirty.c
#include "syscall.h"
int x = 42;
int main() {
    x = 99;          // write → dirty bit debe setearse
    if (x != 99) Exit(1);
    Exit(0);
}