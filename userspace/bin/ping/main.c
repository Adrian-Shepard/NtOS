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

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

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
    printf("\n[ping] starting...\n");

    // Инициализируем нулями, чтобы padding был чистым
    uint8_t frame[64] = {0}; 
    uint8_t dst_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
    uint8_t src_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    
    for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = src_mac[i];
    frame[12] = 0x08;
    frame[13] = 0x00;

    ip_hdr_t *ip = (ip_hdr_t*)(frame + 14);
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(40); // 20 IP + 8 ICMP + 12 Data
    ip->id = htons(0x1234);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = 1;
    ip->src_ip = htonl(0x0A00020F); // 10.0.2.15
    ip->dst_ip = htonl(0x0A000202); // 10.0.2.2
    ip->checksum = 0;
    
    // ФИКС: Возвращаем htons() для конвертации в сетевой порядок
    ip->checksum = htons(checksum(ip, 20));

    icmp_hdr_t *icmp = (icmp_hdr_t*)(frame + 14 + 20);
    icmp->type = 8;
    icmp->code = 0;
    icmp->id = htons(0x1234);
    icmp->seq = htons(1);
    icmp->checksum = 0;

    uint8_t *payload = (uint8_t*)(frame + 14 + 20 + 8);
    for (int i = 0; i < 12; i++) payload[i] = 'A' + i;

    // ФИКС: Возвращаем htons()
    icmp->checksum = htons(checksum(icmp, 20));

    printf("[ping] sending ICMP Echo Request to 10.0.2.2...\n");
    long sent = net_send(frame, 64);
    printf("[ping] sent %d bytes\n", (int)sent);

    uint8_t rx_buf[2048];
    for (int i = 0; i < 30; i++) {
        long n = net_recv(rx_buf, sizeof(rx_buf));
        if (n > 0) {
            if (rx_buf[12] == 0x08 && rx_buf[13] == 0x00) {
                uint8_t *icmp_reply = rx_buf + 14 + 20;
                if (icmp_reply[0] == 0) { // Echo Reply
                    printf("\n[ping] Ping success! Got ICMP Echo Reply:\n");
                    for (int j = 0; j < n; j++) {
                        if (j % 16 == 0) printf("\n  %04x: ", j);
                        printf("%02x ", rx_buf[j]);
                    }
                    printf("\n");
                    return 0;
                }
            }
        }
        sleep(100);
    }

    printf("[ping] no response (timeout)\n");
    return 0;
}