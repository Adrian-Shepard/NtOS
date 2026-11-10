#include "usb.h"
#include "driver/usb/xhci/xhci.h"
#include "bus/pci.h"
#include "log/log.h"
#include "mem/vmm.h"

void usb_init(void) {
    logi(S_DRV, "[USB] Scanning PCI bus for USB controllers...");
    
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read32(bus, slot, func, 0);
                if (id == 0xFFFFFFFF || id == 0) continue;
                
                uint32_t class_rev = pci_read32(bus, slot, func, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;
                uint8_t prog_if = (class_rev >> 8) & 0xFF;

                if (class_code == 0x0C && subclass == 0x03) {
                    logi(S_DRV, "[USB] Found controller ProgIF=0x%x", prog_if);
                    
                    if (prog_if == 0x30) { // xHCI
                        uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
                        uint32_t bar1 = pci_read32(bus, slot, func, 0x14);
                        
                        logi(S_DRV, "[USB] Old BAR0=0x%x BAR1=0x%x", bar0, bar1);

                        // Если BAR0 не содержит валидного адреса (меньше 1 МБ), назначаем вручную
                        if ((bar0 & 0xFFFFFFF0) == 0) {
                            uint32_t new_bar = 0xFEB00000; // Типичный адрес для xHCI в QEMU
                            pci_write32(bus, slot, func, 0x10, new_bar);
                            pci_write32(bus, slot, func, 0x14, 0); // Старшие 32 бита = 0
                            
                            bar0 = pci_read32(bus, slot, func, 0x10);
                            bar1 = pci_read32(bus, slot, func, 0x14);
                            logi(S_DRV, "[USB] Reassigned BAR0=0x%x BAR1=0x%x", bar0, bar1);
                        }

                        if (!(bar0 & 0x1) && bar0 != 0xFFFFFFFF) {
                            uint64_t mmio = (uint64_t)(bar0 & 0xFFFFFFF0);
                            if ((bar0 & 0x6) == 0x4) {
                                mmio |= ((uint64_t)bar1 << 32);
                            }
                            
                            uint32_t cmd = pci_read32(bus, slot, func, 0x04);
                            pci_write32(bus, slot, func, 0x04, cmd | (1 << 1) | (1 << 2));
                            
                            logi(S_DRV, "[USB] Initializing xHCI at MMIO 0x%x", (uint32_t)mmio);
                            xhci_init(mmio);
                        }
                    }
                }
            }
        }
    }
}