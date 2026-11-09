#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip_hdr_t;

uint16_t htons(uint16_t x) {
    return (x >> 8) | (x << 8);
}

uint32_t htonl(uint32_t x) {
    return ((x & 0xFF) << 24) |
           ((x & 0xFF00) << 8) |
           ((x >> 8) & 0xFF00) |
           ((x >> 24) & 0xFF);
}

uint16_t checksum(void *data, int len) {
    uint16_t *buf = (uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) sum += *(uint8_t*)buf;
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

int main(void) {
    printf("\n[udp_test] starting...\n");

    uint8_t frame[64] = {0};
    // MAC-адрес шлюза QEMU (из ARP-ответа)
    uint8_t dst_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02}; 
    // Наш MAC-адрес
    uint8_t src_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56}; 
    
    for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = src_mac[i];
    frame[12] = 0x08; frame[13] = 0x00; // IPv4

    // IP-заголовок
    ip_hdr_t *ip = (ip_hdr_t*)(frame + 14);
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(32); // 20 IP + 8 UDP + 4 Data = 32
    ip->id = htons(0x1234);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = 17; // UDP
    ip->src_ip = htonl(0x0A00020F); // 10.0.2.15
    ip->dst_ip = htonl(0x0A000203); // 10.0.2.3 (DNS сервер QEMU)
    ip->checksum = 0;
    ip->checksum = htons(checksum(ip, 20));

    // UDP-заголовок (8 байт)
    uint8_t *udp = (uint8_t*)(frame + 14 + 20);
    *(uint16_t*)(udp + 0) = htons(1234); // Src Port
    *(uint16_t*)(udp + 2) = htons(53);   // Dst Port (DNS)
    *(uint16_t*)(udp + 4) = htons(12);   // Length (8 + 4)
    *(uint16_t*)(udp + 6) = 0;           // Checksum (0 = нет суммы для IPv4 UDP)

    // Данные (просто мусор)
    udp[8] = 'T'; udp[9] = 'E'; udp[10] = 'S'; udp[11] = 'T';

    printf("[udp_test] sending UDP packet to 10.0.2.3:53...\n");
    long sent = net_send(frame, 64);
    printf("[udp_test] sent %d bytes\n", (int)sent);

    uint8_t rx_buf[2048];
    for (int i = 0; i < 30; i++) {
        long n = net_recv(rx_buf, sizeof(rx_buf));
        if (n > 0) {
            if (rx_buf[12] == 0x08 && rx_buf[13] == 0x00) {
                printf("\n[udp_test] Got IPv4 packet!\n");
                for (int j = 0; j < n; j++) {
                    if (j % 16 == 0) printf("\n  %04x: ", j);
                    printf("%02x ", rx_buf[j]);
                }
                printf("\n");
                return 0;
            }
        }
        sleep(100);
    }

    printf("[udp_test] no response (timeout)\n");
    return 0;
}