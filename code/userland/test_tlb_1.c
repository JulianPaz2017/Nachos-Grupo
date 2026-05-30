#include "syscall.h"

#define ARR_SIZE  60

int main() {
    int arr[ARR_SIZE]; // accede a varias páginas
    int i;

    for (i = 0; i < ARR_SIZE; i++) {
      arr[i] = i;
    }

    for (i = 0; i < ARR_SIZE; i++) {
      if (arr[i] != i) Exit(1); // falla si hay corrupción
    }

    Exit(0);
}