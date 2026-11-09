#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "synth_user.h"

typedef struct {
    int freq;
    int ms;
} audio_msg_t;

int main(void) {
    printf("[audio_server] starting...\n");
    
    long port = ipc_create();
    if (port < 0) {
        printf("[audio_server] failed to create port\n");
        return 1;
    }
    
    kv_write("audio_port", &port, sizeof(port));
    printf("[audio_server] listening on port %d\n", (int)port);
    
    static uint8_t pcm[32768];
    audio_msg_t msg;
    
    for (;;) {
        long n = ipc_recv(port, &msg, sizeof(msg));
        if (n == sizeof(msg)) {
            printf("[audio_server] playing %d Hz for %d ms\n", msg.freq, msg.ms);
            size_t samples = synth_note(pcm, sizeof(pcm), msg.freq, msg.ms, 0, 80);
            if (samples > 0) {
                play_pcm(pcm, samples);
            }
        }
    }
    return 0;
}