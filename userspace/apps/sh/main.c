#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LINE_MAX 128

static char g_line[LINE_MAX];
static int  g_len = 0;

static void prompt(void) {
    printf("ntsh> ");
}

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void cmd_help(void) {
    printf("Nanotrasen Shell (ntsh) built-ins:\n");
    printf("  help    - show this message\n");
    printf("  clear   - clear screen\n");
    printf("  exit    - terminate shell\n");
    printf("Other commands are executed from initrd (e.g., ls.elf, hello.elf)\n");
}

int main(void) {
    printf("\nNtOS Shell (ntsh v0.2)\n");
    printf("type 'help' for built-in commands\n\n");
    prompt();

    for (;;) {
        char c;
        long r = read(0, &c, 1);
        if (r <= 0) continue;

        if (c == '\n') {
            printf("\n");
            g_line[g_len] = 0;
            
            if (g_len > 0) {
                // Встроенные команды
                if (streq(g_line, "exit") || streq(g_line, "quit")) {
                    printf("bye\n");
                    _exit(0);
                }
                else if (streq(g_line, "help")) {
                    cmd_help();
                }
                else if (streq(g_line, "clear")) {
                    // Просто печатаем много переводов строк (в GUI очистит экран)
                    for (int i = 0; i < 50; i++) printf("\n");
                }
                // Внешние программы
                else {
                    int pid = fork();
                    if (pid == 0) {
                        exec(g_line);
                        printf("command not found: %s\n", g_line);
                        _exit(1);
                    } else if (pid > 0) {
                        int status = 0;
                        sys_wait(pid, &status);
                    } else {
                        printf("fork failed\n");
                    }
                }
            }
            
            g_len = 0;
            prompt();
        } else if (c == '\b' || c == 127) {
            if (g_len > 0) { g_len--; printf("\b \b"); }
        } else if (c >= 32 && c < 127) {
            if (g_len < LINE_MAX - 1) {
                g_line[g_len++] = c;
                char t[2] = { c, 0 };
                printf("%s", t);
            }
        }
    }
    return 0;
}