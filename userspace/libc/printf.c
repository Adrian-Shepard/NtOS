#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#define OUTBUF_SIZE 512

static int fmt_int(char *buf, int is_signed, long v, int base, int upper) {
    char tmp[32];
    int i = 0;
    int neg = 0;
    unsigned long uv;

    if (is_signed && v < 0) {
        neg = 1;
        uv = (unsigned long)(-v);
    } else {
        uv = (unsigned long)v;
    }

    if (uv == 0) {
        tmp[i++] = '0';
    } else {
        const char *lo = "0123456789abcdef";
        const char *up = "0123456789ABCDEF";
        const char *d = upper ? up : lo;
        while (uv) {
            tmp[i++] = d[uv % base];
            uv /= base;
        }
    }

    int n = 0;
    if (neg) buf[n++] = '-';
    while (i--) buf[n++] = tmp[i];
    buf[n] = 0;
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    int n = 0;
    while (*fmt) {
        if (*fmt != '%') { buf[n++] = *fmt++; continue; }
        fmt++;

        char c = *fmt++;
        char tmp[256];
        int len = 0;

        switch (c) {
            case '%': tmp[0] = '%'; tmp[1] = 0; len = 1; break;
            case 'c': tmp[0] = (char)va_arg(ap, int); tmp[1] = 0; len = 1; break;
            case 's': {
                const char *s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int slen = (int)strlen(s);
                if (slen > 200) slen = 200;
                for (int i = 0; i < slen; i++) tmp[i] = s[i];
                tmp[slen] = 0;
                len = slen;
                break;
            }
            case 'd':
            case 'i': {
                int v = va_arg(ap, int);
                len = fmt_int(tmp, 1, (long)v, 10, 0);
                break;
            }
            case 'u': {
                unsigned int v = va_arg(ap, unsigned int);
                len = fmt_int(tmp, 0, (long)v, 10, 0);
                break;
            }
            case 'x': {
                unsigned int v = va_arg(ap, unsigned int);
                len = fmt_int(tmp, 0, (long)v, 16, 0);
                break;
            }
            case 'X': {
                unsigned int v = va_arg(ap, unsigned int);
                len = fmt_int(tmp, 0, (long)v, 16, 1);
                break;
            }
            case 'p': {
                unsigned long v = va_arg(ap, unsigned long);
                tmp[0] = '0'; tmp[1] = 'x';
                int hl = fmt_int(tmp + 2, 0, (long)v, 16, 0);
                len = 2 + hl;
                break;
            }
            default:
                tmp[0] = '%'; tmp[1] = c; tmp[2] = 0; len = 2; break;
        }
        for (int i = 0; i < len; i++) buf[n++] = tmp[i];
    }
    buf[n] = 0;
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    char buf[OUTBUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    if (n > 0) write(1, buf, n);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int puts(const char *s) {
    int n = (int)strlen(s);
    write(1, s, n);
    write(1, "\n", 1);
    return n + 1;
}