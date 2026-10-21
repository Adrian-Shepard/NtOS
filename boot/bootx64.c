#include "efi.h"
#include "../bootinfo.h"

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;

static VOID puts16(CHAR16 *s) { ST->ConOut->OutputString(ST->ConOut, s); }

static EFI_STATUS load_file(EFI_HANDLE image, CHAR16 *path, VOID **out_data, UINTN *out_size) {
    EFI_LOADED_IMAGE_PROTOCOL *li;
    GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_STATUS s = BS->HandleProtocol(image, &li_guid, (VOID**)&li);
    if (EFI_ERROR(s)) return s;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    s = BS->HandleProtocol(li->DeviceHandle, &fs_guid, (VOID**)&fs);
    if (EFI_ERROR(s)) return s;

    EFI_FILE_PROTOCOL *root;
    s = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(s)) return s;

    EFI_FILE_PROTOCOL *f;
    s = root->Open(root, &f, path, 1, 0);
    if (EFI_ERROR(s)) return s;

    UINTN cap = 1024 * 1024, size = 0;
    VOID *buf = NULL;
    s = BS->AllocatePool(2, cap, &buf);
    if (EFI_ERROR(s)) return s;

    for (;;) {
        if (size + 4096 > cap) {
            UINTN ncap = cap * 2;
            VOID *nbuf = NULL;
            s = BS->AllocatePool(2, ncap, &nbuf);
            if (EFI_ERROR(s)) { BS->FreePool(buf); return s; }
            for (UINTN i = 0; i < size; i++) ((UINT8*)nbuf)[i] = ((UINT8*)buf)[i];
            BS->FreePool(buf);
            buf = nbuf; cap = ncap;
        }
        UINTN chunk = 4096;
        s = f->Read(f, &chunk, (UINT8*)buf + size);
        if (EFI_ERROR(s)) { BS->FreePool(buf); return s; }
        if (chunk == 0) break;
        size += chunk;
    }
    f->Close(f);
    *out_data = buf;
    *out_size = size;
    return EFI_SUCCESS;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    ST = st; BS = st->BootServices;
    ST->ConOut->ClearScreen(ST->ConOut);
    puts16(L"NtOS/CENTCOM: Nanotrasen Secure Boot\r\n");

    puts16(L"[1] locating GOP...\r\n");
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS s = BS->LocateProtocol(&gop_guid, NULL, (VOID**)&gop);
    
    if (EFI_ERROR(s) || !gop || !gop->Mode || !gop->Mode->Info) { 
        puts16(L"No GOP or GOP Mode Info\r\n"); 
        return 1; 
    }

    puts16(L"[2] reading centcom.bin...\r\n");
    VOID *kdata = NULL; UINTN ksize = 0;
    s = load_file(image, L"\\centcom.bin", &kdata, &ksize);
    if (EFI_ERROR(s)) { puts16(L"centcom.bin not found\r\n"); return s; }
    if (ksize > KERNEL_MAX_SIZE) { puts16(L"kernel too big\r\n"); return 1; }

    puts16(L"[3] allocating kernel + bootinfo page...\r\n");
    EFI_PHYSICAL_ADDRESS kaddr = KERNEL_LOAD_ADDR;
    // Просим выделить страницы по предпочтительному адресу
    s = BS->AllocatePages(2, 2, KERNEL_PAGES + 1, &kaddr);
    if (EFI_ERROR(s)) { puts16(L"alloc kernel pages failed\r\n"); return s; }

    puts16(L"[4] copying kernel...\r\n");
    // Копируем по фактическому адресу kaddr
    for (UINTN i = 0; i < ksize; i++)
        ((UINT8*)kaddr)[i] = ((UINT8*)kdata)[i];

    puts16(L"[5] filling bootinfo at fixed addr...\r\n");
    bootinfo_t *bi = (bootinfo_t*)BOOTINFO_FIXED_ADDR;
    bi->magic     = BOOTINFO_MAGIC;
    bi->fb_addr   = gop->Mode->FrameBufferBase;
    bi->fb_width  = gop->Mode->Info->HorizontalResolution;
    bi->fb_height = gop->Mode->Info->VerticalResolution;
    bi->fb_pitch  = gop->Mode->Info->PixelsPerScanLine * 4;
    bi->fb_bpp    = 32;

    puts16(L"[6] preparing memory map...\r\n");
    UINTN mmap_size = 0, map_key = 0, desc_size = 0; UINT32 desc_ver = 0;
    BS->GetMemoryMap(&mmap_size, 0, &map_key, &desc_size, &desc_ver);
    mmap_size += 64 * desc_size;

    EFI_MEMORY_DESCRIPTOR *mmap = NULL;
    s = BS->AllocatePool(2, mmap_size, (VOID**)&mmap);
    if (EFI_ERROR(s)) { puts16(L"AllocatePool failed\r\n"); return s; }

    puts16(L"[7] final GetMemoryMap + ExitBootServices...\r\n");
    s = BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(s)) return s;

    bi->mmap_addr      = (uint64_t)mmap;
    bi->mmap_size      = mmap_size;
    bi->mmap_desc_size = desc_size;
    bi->mmap_desc_ver  = desc_ver;

    __asm__ volatile("cli");
    s = BS->ExitBootServices(image, map_key);
    if (EFI_ERROR(s)) {
        BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
        s = BS->ExitBootServices(image, map_key);
        if (EFI_ERROR(s)) {
            __asm__ volatile("sti");
            return s;
        }
    }

    bi->magic = BOOTINFO_MAGIC;
    __asm__ volatile("mfence" ::: "memory");

    typedef void (*kentry_t)(bootinfo_t*);
    kentry_t kentry = (kentry_t)kaddr; // Прыгаем по kaddr
    kentry(bi);

    for (;;) __asm__ volatile("hlt");
    return 0;
}