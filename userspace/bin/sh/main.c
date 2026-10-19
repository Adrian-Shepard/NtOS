#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LINE_MAX 128

static char g_line[LINE_MAX];
static int  g_len = 0;

static void prompt(void) {
    printf("ntsh> ");
}

int main(void) {
    printf("\nNtOS Shell (userspace)\n");
    printf("type a program name (e.g. hello.elf, ls.elf)\n");
    printf("type 'exit' to return to kernel\n\n");
    prompt();

    for (;;) {
        char c;
        long r = read(0, &c, 1);
        if (r <= 0) continue;

        if (c == '\n') {
            printf("\n");
            g_line[g_len] = 0;
            
            if (g_len > 0) {
                // Обработка выхода
                if (strcmp(g_line, "exit") == 0 || strcmp(g_line, "quit") == 0) {
                    printf("bye\n");
                    _exit(0);
                }

                int pid = fork();
                if (pid == 0) {
                    // Дочерний процесс
                    exec(g_line);
                    printf("command not found: %s\n", g_line);
                    _exit(1);
                } else if (pid > 0) {
                    // Родительский процесс: ждем завершения ребенка
                    int status = 0;
                    // ИСПРАВЛЕНО: передаем PID и указатель на статус
                    sys_wait(pid, &status); 
                } else {
                    printf("fork failed\n");
                }
            }
            
            g_len = 0;
            prompt();
        } else if (c == '\b' || c == 127) {
            // Обработка Backspace
            if (g_len > 0) {
                g_len--;
                printf("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            // Печатные символы
            if (g_len < LINE_MAX - 1) {
                g_line[g_len++] = c;
                char t[2] = { c, 0 };
                printf("%s", t);
            }
        }
    }
    return 0;
}