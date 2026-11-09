#include <stdio.h>
#include <unistd.h>

#define MAX_FILES 64

int main(void) {
    char name[128];
    int count = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        long n = sys_list(i, name, sizeof(name));
        if (n <= 0) break;
        printf("  %s\n", name);
        count++;
    }

    printf("%d file(s)\n", count);
    return 0;
}