#include "lib.h"
#include "syscall.h"

unsigned
strlen(const char *s)
{
    unsigned i;
    if (s == (void *)0) return 0;
    for (i = 0; s[i] != '\0'; i++);
    return i;
}

void
puts(const char *s)
{
    if (s == (void *)0) return;
    Write(s, strlen(s), CONSOLE_OUTPUT);
    Write("\n", 1, CONSOLE_OUTPUT);
}

void
itoa(int n, char *str)
{
    int i = 0;
    int isNegative = 0;

    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (n < 0) {
        isNegative = 1;
        n = -n;
    }

    while (n != 0) {
        int rem = n % 10;
        str[i++] = rem + '0';
        n = n / 10;
    }

    if (isNegative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}
