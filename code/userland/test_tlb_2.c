// tlb_thrash.c — accede a más páginas que entradas tiene la TLB

#include "syscall.h"
#define N 1024

int arr[N]; // si la TLB tiene 4 entradas, esto fuerza reemplazos

int main() {
    int i;
    for (i = 0; i < N; i++) arr[i] = i * 2;
    for (i = 0; i < N; i++) {
        if (arr[i] != i * 2) Exit(1);
    }
    Exit(0);
}