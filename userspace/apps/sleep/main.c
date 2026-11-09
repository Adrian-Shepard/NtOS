#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: sleep <ms>\n"); return 1; }
    int ms = 0;
    for (int i = 0; argv[1][i]; i++) ms = ms * 10 + (argv[1][i] - '0');
    printf("sleeping for %d ms...\n", ms);
    sleep(ms);
    printf("awake!\n");
    return 0;
}