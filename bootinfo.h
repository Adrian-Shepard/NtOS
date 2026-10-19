#pragma once
#include <stdint.h>

#define BOOTINFO_MAGIC      0x4E544F53u
#define BOOTINFO_FIXED_ADDR 0x4400000ull

typedef struct {
    uint32_t magic;
    uint32_t _pad;
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
    uint64_t mmap_addr;
    uint64_t mmap_size;
    uint64_t mmap_desc_size;
    uint32_t mmap_desc_ver;
} bootinfo_t;

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;