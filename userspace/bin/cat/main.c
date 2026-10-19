#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: cat <file>\n");
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open %s\n", argv[1]);
        return 1;
    }

    char buf[512];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || (c >= 32 && c < 127)) putchar(c);
            else putchar('.');
        }
    }

    close(fd);
    return 0;
}