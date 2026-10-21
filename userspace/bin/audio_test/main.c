#include <stdio.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    int freq;
    int ms;
} audio_msg_t;

int main(void) {
    long port = 0;
    long n = kv_read("audio_port", &port, sizeof(port));
    if (n != sizeof(port)) {
        printf("[audio_test] audio_server not running\n");
        return 1;
    }
    
    printf("[audio_test] sending notes to port %d\n", (int)port);
    
    audio_msg_t msg;
    
    msg.freq = 440; msg.ms = 400; ipc_send(port, &msg, sizeof(msg));
    msg.freq = 554; msg.ms = 400; ipc_send(port, &msg, sizeof(msg));
    msg.freq = 659; msg.ms = 800; ipc_send(port, &msg, sizeof(msg));
    
    sleep(2000);
    printf("[audio_test] done\n");
    return 0;
}