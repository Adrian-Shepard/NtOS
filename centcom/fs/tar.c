#include "tar.h"
#include "../log/log.h"

/* заголовок ustar: 512 байт */
typedef struct {
    char     name[100];
    char     mode[8];
    char     uid[8];
    char     gid[8];
    char     size[12];
    char     mtime[12];
    char     chksum[8];
    char     typeflag;
    char     linkname[100];
    char     magic[6];      /* "ustar\0" */
    char     version[2];    /* "00" */
    char     uname[32];
    char     gname[32];
    char     devmajor[8];
    char     devminor[8];
    char     prefix[155];
} __attribute__((packed)) tar_hdr_t;

static uint64_t oct2bin(const char *s, int n) {
    uint64_t v = 0;
    while (n-- > 0 && *s >= '0' && *s <= '7') {
        v = (v << 3) + (uint64_t)(*s - '0');
        s++;
    }
    return v;
}

void tar_parse(const uint8_t *data, uint64_t size, tar_callback_t cb) {
    uint64_t off = 0;
    int count = 0;

    while (off + 512 <= size) {
        const tar_hdr_t *h = (const tar_hdr_t*)(data + off);

        /* конец архива — блок нулей */
        if (h->name[0] == 0) break;

        /* magic "ustar" (не всегда есть, но у GNU tar есть) */
        if (h->magic[0] && (h->magic[0] != 'u' || h->magic[1] != 's')) {
            break;
        }

        uint64_t fsize = oct2bin(h->size, 12);
        uint64_t padded = ((fsize + 511) / 512) * 512;

        /* обычный файл: typeflag '0' или '\0' */
        if (h->typeflag == '0' || h->typeflag == 0) {
            const uint8_t *fdata = data + off + 512;

            if (off + 512 + fsize > size) {
                loge(S_NTM, "truncated archive at %d", count);
                return;
            }

            if (cb) cb(h->name, fdata, fsize);
            count++;
        }

        off += 512 + padded;
    }

    logi(S_NTM, "tar: %d files parsed", count);
}