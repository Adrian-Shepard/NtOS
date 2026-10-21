#include "elf.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../log/log.h"

static void memcpy8(uint8_t *dst, const uint8_t *src, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) dst[i] = src[i];
}
static void memset0(uint8_t *dst, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) dst[i] = 0;
}

uint64_t elf_load(const uint8_t *data, uint64_t size) {
    if (size < sizeof(elf64_hdr_t)) {
        loge(S_ELF, "file too small");
        return 0;
    }

    const elf64_hdr_t *h = (const elf64_hdr_t*)data;

    if (h->e_ident[0] != 0x7F || h->e_ident[1] != 'E' ||
        h->e_ident[2] != 'L'  || h->e_ident[3] != 'F') {
        loge(S_ELF, "bad magic");
        return 0;
    }
    if (h->e_ident[4] != 2) {
        loge(S_ELF, "not ELF64");
        return 0;
    }
    if (h->e_machine != 0x3E) {
        loge(S_ELF, "not x86_64 (machine=%x)", h->e_machine);
        return 0;
    }

    logi(S_ELF, "ELF64 entry=%x phdrs=%d", h->e_entry, h->e_phnum);

    for (int i = 0; i < h->e_phnum; i++) {
        // Проверка границ заголовка
        if (h->e_phoff + (uint64_t)i * h->e_phentsize + sizeof(elf64_phdr_t) > size) {
            loge(S_ELF, "phdr out of bounds");
            return 0;
        }

        const elf64_phdr_t *ph = (const elf64_phdr_t*)
            (data + h->e_phoff + (uint64_t)i * h->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        // Проверка границ сегмента
        if (ph->p_offset + ph->p_filesz > size) {
            loge(S_ELF, "segment out of file bounds");
            return 0;
        }

        // Проверка на переполнение vaddr + memsz
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) {
            loge(S_ELF, "vaddr + memsz overflow");
            return 0;
        }

        // Проверка выравнивания (должно быть кратно странице)
        if (ph->p_vaddr & 0xFFF) {
            loge(S_ELF, "vaddr not page aligned: %x", ph->p_vaddr);
            // Мы можем просто выровнять вниз и скопировать со смещением, 
            // но для простоты пока просто отклоним.
            return 0;
        }

        logi(S_ELF, "PT_LOAD vaddr=%x filesz=%d memsz=%d",
             ph->p_vaddr, ph->p_filesz, ph->p_memsz);

        uint64_t pages = (ph->p_memsz + 0xFFF) / 0x1000;

        for (uint64_t p = 0; p < pages; p++) {
            uint64_t phys = pmm_alloc();
            if (!phys) { loge(S_ELF, "out of physical memory"); return 0; }

            memset0((uint8_t*)phys, 0x1000);
            vmm_map(ph->p_vaddr + p * 0x1000, phys,
                    PTE_PRESENT | PTE_WRITE | PTE_USER);

            uint64_t off = p * 0x1000;
            if (off < ph->p_filesz) {
                uint64_t chunk = ph->p_filesz - off;
                if (chunk > 0x1000) chunk = 0x1000;
                memcpy8((uint8_t*)phys,
                        data + ph->p_offset + off,
                        chunk);
            }
        }
    }

    return h->e_entry;
}