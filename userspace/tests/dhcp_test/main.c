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
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed)) dhcp_msg_t;

uint16_t htons(uint16_t x) { return (x >> 8) | (x << 8); }
uint32_t htonl(uint32_t x) { return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF); }

uint16_t checksum(void *data, int len) {
    uint16_t *buf = (uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *buf++; len -= 2; }
    if (len == 1) sum += *(uint8_t*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

int main(void) {
    printf("\n[dhcp_test] starting...\n");

    uint8_t frame[512] = {0};
    
    // Ethernet: Broadcast
    for (int i = 0; i < 6; i++) frame[i] = 0xFF; // Dst MAC
    uint8_t src_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    for (int i = 0; i < 6; i++) frame[6 + i] = src_mac[i];
    frame[12] = 0x08; frame[13] = 0x00;

    // IP-заголовок
    ip_hdr_t *ip = (ip_hdr_t*)(frame + 14);
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(300); // 20 IP + 8 UDP + 272 DHCP
    ip->id = htons(0x1234);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = 17; // UDP
    ip->src_ip = 0;    // 0.0.0.0 (Мы пока без IP)
    ip->dst_ip = 0xFFFFFFFF; // 255.255.255.255 (Broadcast)
    ip->checksum = 0;
    ip->checksum = htons(checksum(ip, 20));

    // UDP-заголовок
    udp_hdr_t *udp = (udp_hdr_t*)(frame + 14 + 20);
    udp->src_port = htons(68); // DHCP Client
    udp->dst_port = htons(67); // DHCP Server
    udp->length = htons(280);  // 8 UDP + 272 DHCP
    udp->checksum = 0;         // В IPv4 UDP checksum опционален

    // DHCP Payload
    dhcp_msg_t *dhcp = (dhcp_msg_t*)(frame + 14 + 20 + 8);
    dhcp->op = 1; // Boot Request
    dhcp->htype = 1; // Ethernet
    dhcp->hlen = 6;
    dhcp->xid = htonl(0x12345678); // Transaction ID
    dhcp->flags = htons(0x8000);   // Broadcast flag
    for (int i = 0; i < 6; i++) dhcp->chaddr[i] = src_mac[i]; // Наш MAC
    dhcp->magic = htonl(0x63825363); // DHCP Magic Cookie

    // Опции DHCP
    dhcp->options[0] = 53; // DHCP Message Type
    dhcp->options[1] = 1;  // Length
    dhcp->options[2] = 1;  // Discover
    dhcp->options[3] = 255; // End Option

    printf("[dhcp_test] sending DHCP Discover...\n");
    net_send(frame, 300);

    uint8_t rx_buf[2048];
    for (int i = 0; i < 30; i++) {
        long n = net_recv(rx_buf, sizeof(rx_buf));
        if (n > 0) {
            if (rx_buf[12] == 0x08 && rx_buf[13] == 0x00) { // IPv4
                uint8_t *ip_hdr = rx_buf + 14;
                if (ip_hdr[9] == 17) { // UDP
                    uint8_t *udp_hdr = ip_hdr + 20;
                    if (udp_hdr[0] == 0 && udp_hdr[1] == 67) { // Из порта 67
                        printf("\n[dhcp_test] SUCCESS! Got DHCP Offer!\n");
                        printf("Your IP is: %d.%d.%d.%d\n",
                               rx_buf[14+20+8+16], rx_buf[14+20+8+17],
                               rx_buf[14+20+8+18], rx_buf[14+20+8+19]);
                        return 0;
                    }
                }
            }
        }
        sleep(100);
    }

    printf("[dhcp_test] no response (timeout)\n");
    return 0;
}