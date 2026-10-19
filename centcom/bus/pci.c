#include "pci.h"
#include "../core/io.h"
#include "../log/log.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)(
        (1U << 31) | 
        ((uint32_t)bus << 16) | 
        ((uint32_t)slot << 11) | 
        ((uint32_t)func << 8) | 
        (offset & 0xFC)
    );
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)(
        (1U << 31) | 
        ((uint32_t)bus << 16) | 
        ((uint32_t)slot << 11) | 
        ((uint32_t)func << 8) | 
        (offset & 0xFC)
    );
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, val);
}

int pci_find_device(uint8_t class_code, uint8_t subclass, pci_device_t *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t reg0 = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0);
                if ((reg0 & 0xFFFF) == 0xFFFF) {
                    if (func == 0) break;  /* single-function device */
                    continue;
                }

                uint32_t reg8 = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                uint8_t c = (reg8 >> 24) & 0xFF;
                uint8_t s = (reg8 >> 16) & 0xFF;
                uint8_t p = (reg8 >> 8) & 0xFF;

                if (c == class_code && s == subclass) {
                    out->bus = (uint8_t)bus;
                    out->slot = (uint8_t)slot;
                    out->func = (uint8_t)func;
                    out->vendor_id = reg0 & 0xFFFF;
                    out->device_id = (reg0 >> 16) & 0xFFFF;
                    out->class_code = c;
                    out->subclass = s;
                    out->prog_if = p;
                    uint32_t reg0c = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x0C);
                    out->header_type = (reg0c >> 16) & 0xFF;
                    for (int i = 0; i < 6; i++)
                        out->bar[i] = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x10 + i * 4);
                    return 1;
                }
            }
        }
    }
    return 0;
}