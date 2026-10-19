#include <stdio.h>

int main(int argc, char **argv) {
    printf("hello from ELF64 userland\n");
    printf("  argc = %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("    argv[%d] = [%s]\n", i, argv[i]);
    printf("  printf works:\n");
    printf("    int:    %d + %d = %d\n", 2, 3, 5);
    printf("    hex:    0x%x\n", 0xdeadbeef);
    printf("    upper:  0x%X\n", 0xcafe);
    printf("    ptr:    %p\n", (void*)0x10000000000UL);
    return 0;
}