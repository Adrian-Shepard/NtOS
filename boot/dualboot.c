#include <stdint.h>
#include "../bootinfo.h"

typedef struct { uint32_t type; uint32_t size; } mb2_tag_t;

typedef struct {
    uint32_t type; uint32_t size;
    uint64_t addr; uint32_t pitch; uint32_t width; uint32_t height;
    uint8_t bpp; uint8_t fb_type; uint16_t reserved;
} mb2_tag_fb_t;

typedef struct {
    uint32_t type; uint32_t size;
    uint32_t entry_size; uint32_t entry_version;
} mb2_tag_mmap_t;

typedef struct {
    uint64_t addr; uint64_t len; uint32_t type; uint32_t zero;
} e820_entry_t;

extern void centcom_boot(bootinfo_t *bi);

static EFI_MEMORY_DESCRIPTOR efi_mmap[64];

void dualboot_main(uint32_t mb_info, uint32_t magic) {
    if (magic != 0x36d76289) {
        for (;;) __asm__ volatile("cli; hlt");
    }

    bootinfo_t *bi = (bootinfo_t*)BOOTINFO_FIXED_ADDR;
    bi->magic = BOOTINFO_MAGIC;
    
    // Дефолтные значения (на случай, если GRUB не передаст тег)
    bi->fb_addr = 0x80000000; // Стандартный адрес VGA в QEMU
    bi->fb_width = 1024;
    bi->fb_height = 768;
    bi->fb_pitch = 1024 * 4;
    bi->fb_bpp = 32;

    mb2_tag_t *tag = (mb2_tag_t*)(uintptr_t)(mb_info + 8);
    int mmap_count = 0;

    while (tag->type != 0) {
        if (tag->type == 8) { // Framebuffer
            mb2_tag_fb_t *fb = (mb2_tag_fb_t*)tag;
            bi->fb_addr = fb->addr;
            bi->fb_width = fb->width;
            bi->fb_height = fb->height;
            bi->fb_pitch = fb->pitch;
            bi->fb_bpp = 32; // ПРИНУДИТЕЛЬНО 32 БИТА
        } 
        else if (tag->type == 6) { // Memory Map (E820)
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t*)tag;
            uint8_t *ptr = (uint8_t*)mmap + sizeof(mb2_tag_mmap_t);
            uint8_t *end = (uint8_t*)mmap + mmap->size;
            
            while (ptr < end && mmap_count < 64) {
                e820_entry_t *e = (e820_entry_t*)ptr;
                
                efi_mmap[mmap_count].Type = (e->type == 1) ? 7 : 0;
                efi_mmap[mmap_count].PhysicalStart = e->addr;
                efi_mmap[mmap_count].NumberOfPages = (e->len + 0xFFF) / 0x1000;
                
                mmap_count++;
                ptr += mmap->entry_size;
            }
        }
        
        tag = (mb2_tag_t*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }

    bi->mmap_addr = (uint64_t)efi_mmap;
    bi->mmap_size = mmap_count * sizeof(EFI_MEMORY_DESCRIPTOR);
    bi->mmap_desc_size = sizeof(EFI_MEMORY_DESCRIPTOR);
    bi->mmap_desc_ver = 0;

    centcom_boot(bi);
    
    for (;;) __asm__ volatile("cli; hlt");
}