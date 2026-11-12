/* doom_libc.c — мини-libc для порта Doom в userspace.
 * Собирается ВМЕСТО libc.a: линкуются только crt0.o + syscall.o, поэтому
 * здесь реализованы строки, математика, printf и слой FILE (через open/read). */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

int errno = 0;

/* ============================ HEAP ============================ */
#define HEAP_SIZE (32 * 1024 * 1024)
static uint8_t heap_mem[HEAP_SIZE] __attribute__((aligned(16)));

typedef struct Block {
    size_t       size;
    int          free;
    struct Block *next;
} Block;

static Block *heap_start = 0;

static void heap_init(void) {
    heap_start = (Block *)heap_mem;
    heap_start->size = HEAP_SIZE - sizeof(Block);
    heap_start->free = 1;
    heap_start->next = 0;
}

void *doom_malloc(size_t size) {
    if (!heap_start) heap_init();
    if (size == 0) size = 1;
    size = (size + 15) & ~(size_t)15;   /* выравнивание 16 байт */
    Block *b = heap_start;
    while (b) {
        if (b->free && b->size >= size) {
            if (b->size >= size + sizeof(Block) + 16) {
                Block *nb = (Block *)((uint8_t *)(b + 1) + size);
                nb->size = b->size - size - sizeof(Block);
                nb->free = 1;
                nb->next = b->next;
                b->size = size;
                b->next = nb;
            }
            b->free = 0;
            return (void *)(b + 1);
        }
        b = b->next;
    }
    return 0;
}

void doom_free(void *p) {
    if (!p) return;
    Block *b = (Block *)p - 1;
    b->free = 1;
    /* слияние со следующими свободными блоками */
    Block *n = b->next;
    while (n && n->free) {
        b->size += sizeof(Block) + n->size;
        b->next = n->next;
        n = b->next;
    }
}

void *doom_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = doom_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *doom_realloc(void *p, size_t size) {
    if (!p) return doom_malloc(size);
    Block *b = (Block *)p - 1;
    if (b->size >= size) return p;
    void *np = doom_malloc(size);
    if (np) {
        size_t cp = b->size < size ? b->size : size;
        memcpy(np, p, cp);
        doom_free(p);
    }
    return np;
}
/* ============================ STRINGS ============================ */
void *memcpy(void *d, const void *s, size_t n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d, int c, size_t n){unsigned char *p=d;while(n--)*p++=(unsigned char)c;return d;}
void *memmove(void *d, const void *s, size_t n){unsigned char *a=d;const unsigned char *b=s;if(a<b){while(n--)*a++=*b++;}else{a+=n;b+=n;while(n--)*--a=*--b;}return d;}
int memcmp(const void *a, const void *b, size_t n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
int strcmp(const char *a, const char *b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char *a, const char *b, size_t n){while(n&&*a&&*a==*b){a++;b++;n--;}if(!n)return 0;return (unsigned char)*a-(unsigned char)*b;}
char *strcpy(char *d, const char *s){char *r=d;while((*d++=*s++));return r;}
char *strncpy(char *d, const char *s, size_t n){char *r=d;while(n&&(*d=*s)){d++;s++;n--;}while(n--)*d++=0;return r;}
char *strcat(char *d, const char *s){char *r=d;while(*d)d++;while((*d++=*s++));return r;}
char *strchr(const char *s, int c){while(*s){if(*s==(char)c)return (char*)s;s++;}return c==0?(char*)s:0;}
char *strrchr(const char *s, int c){const char *l=0;while(*s){if(*s==(char)c)l=s;s++;}return c==0?(char*)s:(char*)l;}
char *strstr(const char *h, const char *n){if(!*n)return (char*)h;for(;*h;h++){const char *a=h,*b=n;while(*a&&*b&&*a==*b){a++;b++;}if(!*b)return (char*)h;}return 0;}
char *strdup(const char *s){size_t n=strlen(s)+1;char *p=doom_malloc(n);if(p)memcpy(p,s,n);return p;}
char *strupr(char *s){for(char *p=s;*p;p++)if(*p>='a'&&*p<='z')*p-=32;return s;}
char *strlwr(char *s){for(char *p=s;*p;p++)if(*p>='A'&&*p<='Z')*p+=32;return s;}
int strcasecmp(const char *a, const char *b){while(*a&&*b){int ca=(*a>='A'&&*a<='Z')?*a+32:*a;int cb=(*b>='A'&&*b<='Z')?*b+32:*b;if(ca!=cb)return ca-cb;a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncasecmp(const char *a, const char *b, size_t n){while(n&&*a&&*b){int ca=(*a>='A'&&*a<='Z')?*a+32:*a;int cb=(*b>='A'&&*b<='Z')?*b+32:*b;if(ca!=cb)return ca-cb;a++;b++;n--;}if(!n)return 0;return (unsigned char)*a-(unsigned char)*b;}

/* ============================ ЧИСЛА ============================ */
int atoi(const char *s){int sign=1,r=0;while(*s==' '||*s=='\t')s++;if(*s=='-'){sign=-1;s++;}else if(*s=='+')s++;while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;}return sign*r;}
double atof(const char *s){double r=0,sign=1;while(*s==' ')s++;if(*s=='-'){sign=-1;s++;}else if(*s=='+')s++;while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;}if(*s=='.'){s++;double f=0.1;while(*s>='0'&&*s<='9'){r+=(*s-'0')*f;f*=0.1;s++;}}return sign*r;}
int abs(int x){return x<0?-x:x;}
long labs(long x){return x<0?-x:x;}

/* ============================ МАТЕМАТИКА (x87) ============================ */
double sqrt(double x){double r;__asm__("fsqrt":"=t"(r):"0"(x));return r;}
double sin(double x){double r;__asm__("fsin":"=t"(r):"0"(x));return r;}
double cos(double x){double r;__asm__("fcos":"=t"(r):"0"(x));return r;}
double fabs(double x){return x<0?-x:x;}
double floor(double x){long i=(long)x;return (double)((x<i)?i-1:i);}
double ceil(double x){long i=(long)x;return (double)((x>i)?i+1:i);}
double fmod(double x,double y){long t=(long)(x/y);return x-y*(double)t;}
double tan(double x){return sin(x)/cos(x);}
double atan2(double y,double x){double r;__asm__ volatile("fpatan":"=t"(r):"0"(x),"u"(y):"st(1)");return r;}
double atan(double x){return atan2(x,1.0);}
double asin(double x){return atan2(x,sqrt(1-x*x));}
double acos(double x){return atan2(sqrt(1-x*x),x);}
double exp(double x){double y=x*1.4426950408889634,r;__asm__ volatile("fld %%st(0)\nfrndint\nfsub %%st(0),%%st(1)\nfxch\nf2xm1\nfld1\nfaddp\nfscale\nfstp %%st(1)\n":"=t"(r):"0"(y):"st(1)");return r;}
double pow(double x,double y){if(y==0)return 1;if(y==1)return x;if(y==2)return x*x;if(y==0.5)return sqrt(x);double r;__asm__ volatile("fyl2x":"=t"(r):"0"(x),"u"(y):"st(1)");return exp(r*0.6931471805599453);}
double log(double x){double r;__asm__ volatile("fldln2\nfxch\nfyl2x":"=t"(r):"0"(x):"st(1)");return r;}
double log10(double x){double r;__asm__ volatile("fldlg2\nfxch\nfyl2x":"=t"(r):"0"(x):"st(1)");return r;}

/* ============================ CTYPE ============================ */
int toupper(int c){return (c>='a'&&c<='z')?c-32:c;}
int tolower(int c){return (c>='A'&&c<='Z')?c+32:c;}
int isspace(int c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';}
int isdigit(int c){return c>='0'&&c<='9';}
int isalpha(int c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
int isalnum(int c){return isalpha(c)||isdigit(c);}
int isupper(int c){return c>='A'&&c<='Z';}
int islower(int c){return c>='a'&&c<='z';}
int isprint(int c){return c>=0x20&&c<0x7f;}
int ispunct(int c){return isprint(c)&&!isalnum(c)&&c!=' ';}
int isxdigit(int c){return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
/* ============================ PRINTF ============================ */
static void out_write(const char *s, size_t n) { write(1, s, n); }

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    char *p = buf;
    char *e = buf + n - 1;
    while (*fmt) {
        if (*fmt != '%') { if (p < e) *p++ = *fmt++; else fmt++; continue; }
        fmt++;
        int zero_pad = 0, left = 0, width = 0, prec = -1;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt-'0'); fmt++; }
        if (*fmt == '.') { fmt++; prec = 0; while (*fmt >= '0' && *fmt <= '9') { prec = prec*10 + (*fmt-'0'); fmt++; } }

        char out[64]; int len = 0;
        const char *hx = "0123456789abcdef";

        switch (*fmt) {
            case 'd': case 'i': {
                int v = va_arg(ap, int); int neg = 0; unsigned uv;
                if (v < 0) { neg = 1; uv = (unsigned)(-(long)v); } else uv = (unsigned)v;
                char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = '0' + uv%10; uv /= 10; }
                while (c < prec && c < 31) t[c++] = '0';
                if (neg) t[c++] = '-';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'u': {
                unsigned uv = va_arg(ap, unsigned); char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = '0' + uv%10; uv /= 10; }
                while (c < prec && c < 31) t[c++] = '0';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'x': case 'X': {
                unsigned uv = va_arg(ap, unsigned);
                const char *h = (*fmt == 'X') ? "0123456789ABCDEF" : hx;
                char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = h[uv & 0xF]; uv >>= 4; }
                while (c < prec && c < 31) t[c++] = '0';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'p': {
                unsigned long uv = (unsigned long)va_arg(ap, void *); char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = hx[uv & 0xF]; uv >>= 4; }
                t[c++] = 'x'; t[c++] = '0';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'c': { out[len++] = (char)va_arg(ap, int); break; }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && (prec < 0 || len < prec) && len < (int)sizeof(out)-1) out[len++] = *s++;
                break;
            }
            case 'f': {
                double d = va_arg(ap, double); int ip = (int)d; double fp = d - ip;
                if (ip < 0) { if (len < 31) out[len++] = '-'; ip = -ip; fp = -fp; }
                char t[32]; int c = 0;
                if (!ip) t[c++] = '0';
                while (ip) { t[c++] = '0' + ip%10; ip /= 10; }
                while (c > 0) out[len++] = t[--c];
                if (prec < 0) prec = 6;
                if (prec > 0 && len < 31) out[len++] = '.';
                for (int i = 0; i < prec && len < 31; i++) { fp *= 10.0; int dg = (int)fp; out[len++] = '0' + dg; fp -= dg; }
                break;
            }
            case '%': { out[len++] = '%'; break; }
            default:  { out[len++] = '%'; if (*fmt) out[len++] = *fmt; break; }
        }
        fmt++;
        int pad = width - len;
        if (!left) while (pad-- > 0 && p < e) *p++ = zero_pad ? '0' : ' ';
        for (int i = 0; i < len; i++) if (p < e) *p++ = out[i];
        if (left) while (pad-- > 0 && p < e) *p++ = ' ';
    }
    if (n) *p = 0;
    return (int)(p - buf);
}
int snprintf(char *b, size_t n, const char *f, ...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r;}
int sprintf(char *b, const char *f, ...){va_list a;va_start(a,f);int r=vsnprintf(b,4096,f,a);va_end(a);return r;}
int printf(const char *f, ...){char b[512];va_list a;va_start(a,f);int r=vsnprintf(b,sizeof(b),f,a);va_end(a);out_write(b,(size_t)r);return r;}
int fprintf(FILE *s, const char *f, ...){char b[512];va_list a;va_start(a,f);int r=vsnprintf(b,sizeof(b),f,a);va_end(a);(void)s;out_write(b,(size_t)r);return r;}
int vfprintf(FILE *f, const char *fmt, va_list ap){char buf[512];int r=vsnprintf(buf,sizeof(buf),fmt,ap);(void)f;out_write(buf,(size_t)r);return r;}
int puts(const char *s){out_write(s,strlen(s));out_write("\n",1);return 0;}
int putchar(int c){char b[2]={(char)c,0};out_write(b,1);return c;}
int fflush(FILE *f){(void)f;return 0;}
int fputs(const char *s, FILE *f){(void)f;out_write(s,strlen(s));return 0;}
int putc(int c, FILE *f){(void)f;char b[2]={(char)c,0};out_write(b,1);return c;}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int assigned = 0;
    while (*fmt) {
        if (*fmt == ' ' || *fmt == '\t') { fmt++; continue; }
        if (*fmt != '%') { if (*str == *fmt) { str++; fmt++; } else break; continue; }
        fmt++;
        switch (*fmt) {
            case 'x': case 'X': {
                while (*str == ' ' || *str == '\t') str++;
                unsigned v = 0;
                while ((*str >= '0' && *str <= '9') || (*str >= 'a' && *str <= 'f') || (*str >= 'A' && *str <= 'F')) {
                    char c = *str++; v = v * 16 + (c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
                }
                *va_arg(ap, unsigned *) = v; assigned++; break;
            }
            case 'd': case 'i': {
                while (*str == ' ' || *str == '\t') str++;
                int sign = 1, v = 0;
                if (*str == '-') { sign = -1; str++; } else if (*str == '+') str++;
                while (*str >= '0' && *str <= '9') v = v * 10 + (*str++ - '0');
                *va_arg(ap, int *) = sign * v; assigned++; break;
            }
            case 's': {
                char *out = va_arg(ap, char *);
                while (*str == ' ' || *str == '\t') str++;
                while (*str && *str != ' ' && *str != '\t' && *str != '\n') *out++ = *str++;
                *out = 0; assigned++; break;
            }
            case 'c': *va_arg(ap, char *) = *str++; assigned++; break;
            case '%': if (*str == '%') str++; break;
        }
        fmt++;
    }
    va_end(ap); return assigned;
}

/* ============================ RAND / QSORT / EXIT ============================ */
static unsigned long _seed = 1;
int rand(void){_seed=_seed*1103515245+12345;return (_seed>>16)&0x7FFF;}
void srand(unsigned s){_seed=s;}
/* Корректная сортировка вставками (простая и без доп. памяти для любых sz).
   Для типов больше 256 байт ничего не делаем — Doom такие не сортирует. */
void qsort(void *base, size_t nm, size_t sz, int (*cmp)(const void*, const void*)) {
    if (nm < 2 || sz == 0 || sz > 256) return;
    unsigned char *b = base;
    unsigned char tmp[256];
    for (size_t i = 1; i < nm; i++) {
        memcpy(tmp, b + i * sz, sz);
        size_t j = i;
        while (j > 0 && cmp(b + (j - 1) * sz, tmp) > 0) {
            memcpy(b + j * sz, b + (j - 1) * sz, sz);
            j--;
        }
        memcpy(b + j * sz, tmp, sz);
    }
}
void exit(int c) { _exit(c); }
/* ============================ FILE (через syscall'ы) ============================ */
#define DOOM_MAX_FILES 8
struct _dg_file { uint8_t *data; size_t size; size_t pos; int used; };
static struct _dg_file g_files[DOOM_MAX_FILES];

FILE *fopen(const char *name, const char *mode) {
    (void)mode;
    int fd = open(name, 0);
    if (fd < 0) return 0;
    struct _dg_file *f = 0;
    for (int i = 0; i < DOOM_MAX_FILES; i++) if (!g_files[i].used) { f = &g_files[i]; break; }
    if (!f) { close(fd); return 0; }

    size_t cap = 65536, total = 0;
    uint8_t *buf = doom_malloc(cap);
    if (!buf) { close(fd); return 0; }
    for (;;) {
        long r = read(fd, buf + total, cap - total);
        if (r <= 0) break;
        total += (size_t)r;
        if (total == cap) {
            size_t ncap = cap * 2;
            uint8_t *nb = doom_realloc(buf, ncap);
            if (!nb) break;
            buf = nb; cap = ncap;
        }
    }
    close(fd);
    f->data = buf; f->size = total; f->pos = 0; f->used = 1;
    return (FILE *)f;
}

int fclose(FILE *fp) { if (fp) { struct _dg_file *f=(void*)fp; doom_free(f->data); f->data=0; f->used=0; } return 0; }
size_t fread(void *buf, size_t sz, size_t n, FILE *fp) { struct _dg_file *f=(void*)fp; size_t need=sz*n; if(f->pos+need>f->size) need=f->size-f->pos; memcpy(buf,f->data+f->pos,need); f->pos+=need; return need/sz; }
size_t fwrite(const void *b, size_t s, size_t n, FILE *f){(void)b;(void)s;(void)n;(void)f;return 0;}
int fseek(FILE *fp, long off, int whence) { struct _dg_file *f=(void*)fp; long np=off; if(whence==SEEK_CUR) np=(long)f->pos+off; else if(whence==SEEK_END) np=(long)f->size+off; if(np<0) np=0; if((size_t)np>f->size) np=(long)f->size; f->pos=(size_t)np; return 0; }
long ftell(FILE *fp) { struct _dg_file *f=(void*)fp; return (long)f->pos; }
int fgetc(FILE *fp) { struct _dg_file *f=(void*)fp; if(f->pos>=f->size)return EOF; return f->data[f->pos++]; }
char *fgets(char *b, int n, FILE *fp) { int i=0; while(i<n-1){ int c=fgetc(fp); if(c==EOF)break; b[i++]=(char)c; if(c=='\n')break; } b[i]=0; return i?b:0; }
int fputc(int c, FILE *f){(void)c;(void)f;return 0;}

/* ============================ STUBS ============================ */
int remove(const char *path){(void)path;return 0;}
int rename(const char *a, const char *b){(void)a;(void)b;return 0;}
int mkstemp(char *tmpl){(void)tmpl;return -1;}
int access(const char *p, int m){(void)p;(void)m;return -1;}
int stat(const char *p, struct stat *s){(void)p;(void)s;errno=ENOENT;return -1;}
int mkdir(const char *p, mode_t m){(void)p;(void)m;return -1;}
int system(const char *cmd){(void)cmd;return -1;}




