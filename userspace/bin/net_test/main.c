#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    printf("\n[net_test] starting...\n");
    
    // ARP Request для шлюза QEMU (10.0.2.2)
    uint8_t arp_req[42] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Dst MAC (Broadcast)
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56, // Src MAC
        0x08, 0x06,                         // Type: ARP
        0x00, 0x01, 0x08, 0x00,             // HW:Eth, Proto:IPv4
        0x06, 0x04,                         // HW Len, Proto Len
        0x00, 0x01,                         // Opcode: Request
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56, // Sender MAC
        0x0A, 0x00, 0x02, 0x0F,             // Sender IP (10.0.2.15)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Target MAC (zero)
        0x0A, 0x00, 0x02, 0x02              // Target IP (10.0.2.2)
    };

    printf("[net_test] sending ARP request for 10.0.2.2...\n");
    long sent = net_send(arp_req, sizeof(arp_req));
    printf("[net_test] sent %d bytes\n", (int)sent);

    uint8_t rx_buf[2048];
    for (int i = 0; i < 20; i++) {
        long n = net_recv(rx_buf, sizeof(rx_buf));
        if (n > 0) {
            printf("[net_test] received %d bytes:\n", (int)n);
            for (int j = 0; j < n; j++) {
                if (j % 16 == 0) printf("\n  %04x: ", j);
                printf("%02x ", rx_buf[j]);
            }
            printf("\n");
            return 0;
        }
        sleep(100);
    }
    
    printf("[net_test] no response (timeout)\n");
    return 0;
}