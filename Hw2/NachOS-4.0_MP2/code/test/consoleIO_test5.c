#include "syscall.h"

int main()
{
    int arr[10000];
    for (int i = 0; i < 10000; i++)
    {
        arr[i] = i;
    }
    int n;
    for (n = 26; n <= 30; n++)
    {
        PrintInt(n);
    }
    return 0;
    // Halt();
}