#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>

extern const uint8_t doom1_wad_start[];
extern const uint8_t doom1_wad_end[];
extern volatile int doom_should_exit;

// Используем безопасную кучу ядра (адрес 0x5000000)
extern void* kmalloc(size_t size);
extern void  kfree(void* ptr);
extern uint64_t ksize(void* ptr);

int errno = 0;

void* doom_malloc(size_t size) { return kmalloc(size); }
void  doom_free(void* ptr) { kfree(ptr); }

void* doom_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* doom_realloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    void* p = kmalloc(size);
    if (p) {
        size_t old = (size_t)ksize(ptr);   /* не читаем за границей старого блока */
        if (old > size) old = size;
        memcpy(p, ptr, old);
        kfree(ptr);
    }
    return p;
}

// ============ СТРОКИ ============
void* memset(void* d, int c, size_t n){unsigned char* p=d;while(n--)*p++=(unsigned char)c;return d;}
void* memcpy(void* d, const void* s, size_t n){unsigned char* a=d;const unsigned char* b=s;while(n--)*a++=*b++;return d;}
void* memmove(void* d, const void* s, size_t n){
    unsigned char* a=d;const unsigned char* b=s;
    if(a<b){while(n--)*a++=*b++;}
    else{a+=n;b+=n;while(n--)*--a=*--b;}
    return d;
}
int memcmp(const void* a, const void* b, size_t n){
    const unsigned char*x=a,*y=b;
    while(n--){if(*x!=*y)return *x-*y;x++;y++;}
    return 0;
}
size_t strlen(const char* s){size_t n=0;while(s[n])n++;return n;}
int strcmp(const char* a, const char* b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char* a, const char* b, size_t n){while(n&&*a&&*a==*b){a++;b++;n--;}if(!n)return 0;return (unsigned char)*a-(unsigned char)*b;}
char* strcpy(char* d, const char* s){char* r=d;while((*d++=*s++));return r;}
char* strncpy(char* d, const char* s, size_t n){char* r=d;while(n&&(*d=*s)){d++;s++;n--;}while(n--)*d++=0;return r;}
char* strcat(char* d, const char* s){char* r=d;while(*d)d++;while((*d++=*s++));return r;}
char* strchr(const char* s, int c){while(*s){if(*s==(char)c)return (char*)s;s++;}return c==0?(char*)s:0;}
char* strrchr(const char* s, int c){const char* l=0;while(*s){if(*s==(char)c)l=s;s++;}return c==0?(char*)s:(char*)l;}
char* strstr(const char* h, const char* n){if(!*n)return (char*)h;for(;*h;h++){const char*a=h,*b=n;while(*a&&*b&&*a==*b){a++;b++;}if(!*b)return (char*)h;}return 0;}
char* strdup(const char* s){size_t n=strlen(s)+1;char*p=kmalloc(n);if(p)memcpy(p,s,n);return p;}
char* strupr(char* s){for(char*p=s;*p;p++)if(*p>='a'&&*p<='z')*p-=32;return s;}
char* strlwr(char* s){for(char*p=s;*p;p++)if(*p>='A'&&*p<='Z')*p+=32;return s;}

int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncasecmp(const char* a, const char* b, size_t n) {
    while (n && *a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++; n--;
    }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

// ============ ЧИСЛА ============
int atoi(const char* s){int sign=1,r=0;while(*s==' '||*s=='\t')s++;if(*s=='-'){sign=-1;s++;}else if(*s=='+')s++;while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;}return sign*r;}
double atof(const char* s){double r=0,sign=1;while(*s==' ')s++;if(*s=='-'){sign=-1;s++;}else if(*s=='+')s++;while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;}if(*s=='.'){s++;double f=0.1;while(*s>='0'&&*s<='9'){r+=(*s-'0')*f;f*=0.1;s++;}}return sign*r;}
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

// ============ МАТЕМАТИКА (x87) ============
double sqrt(double x){double r;__asm__("fsqrt":"=t"(r):"0"(x));return r;}
double sin (double x){double r;__asm__("fsin" :"=t"(r):"0"(x));return r;}
double cos (double x){double r;__asm__("fcos" :"=t"(r):"0"(x));return r;}
double fabs(double x){return x<0?-x:x;}
double floor(double x){long i=(long)x;return (double)((x<i)?i-1:i);}
double ceil (double x){long i=(long)x;return (double)((x>i)?i+1:i);}
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

// ============ CTYPE ============
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

// ============ ПРЯМОЙ ВЫВОД В COM1 ============
static inline void _outb(unsigned short port, unsigned char val) { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
static void serial_write_str(const char* s) { while (*s) _outb(0x3F8, (unsigned char)*s++); }

// ============ PRINTF ============
int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    char* p = buf;
    char* e = buf + n - 1;
    while (*fmt) {
        if (*fmt != '%') { if (p < e) *p++ = *fmt++; else fmt++; continue; }
        fmt++;
        int zero_pad = 0, left = 0, width = 0, prec = -1;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt-'0'); fmt++; }
        if (*fmt == '.') { fmt++; prec = 0; while (*fmt >= '0' && *fmt <= '9') { prec = prec*10 + (*fmt-'0'); fmt++; } }

        char out[64]; int len = 0;
        const char* hx = "0123456789abcdef";

        switch (*fmt) {
            case 'd': case 'i': {
                int v = va_arg(ap, int); int neg = 0; unsigned uv;
                if (v < 0) { neg = 1; uv = (unsigned)(-(long)v); } else uv = (unsigned)v;
                char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = '0' + uv%10; uv /= 10; }
                while (c < prec) t[c++] = '0';
                if (neg) t[c++] = '-';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'u': {
                unsigned uv = va_arg(ap, unsigned); char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = '0' + uv%10; uv /= 10; }
                while (c < prec) t[c++] = '0';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'x': case 'X': {
                unsigned uv = va_arg(ap, unsigned);
                const char* h = (*fmt == 'X') ? "0123456789ABCDEF" : hx;
                char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = h[uv & 0xF]; uv >>= 4; }
                while (c < prec) t[c++] = '0';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'p': {
                unsigned long uv = (unsigned long)va_arg(ap, void*); char t[32]; int c = 0;
                if (!uv) t[c++] = '0';
                while (uv) { t[c++] = hx[uv & 0xF]; uv >>= 4; }
                t[c++] = 'x'; t[c++] = '0';
                while (c > 0) out[len++] = t[--c];
                break;
            }
            case 'c': { out[len++] = (char)va_arg(ap, int); break; }
            case 's': {
                const char* s = va_arg(ap, const char*);
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
    return p - buf;
}

int snprintf(char* b, size_t n, const char* f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r;}
int sprintf(char* b, const char* f,...){va_list a;va_start(a,f);int r=vsnprintf(b,4096,f,a);va_end(a);return r;}
int printf(const char* f,...){char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);serial_write_str(b);return 0;}
int fprintf(FILE* s, const char* f,...){(void)s;(void)f;return 0;}
int vfprintf(FILE* f, const char* fmt, va_list ap) { (void)f; char buf[512]; int r = vsnprintf(buf, sizeof(buf), fmt, ap); serial_write_str(buf); return r; }
int puts(const char* s) { serial_write_str(s); serial_write_str("\n"); return 0; }
int putchar(int c) { char b[2] = { (char)c, 0 }; serial_write_str(b); return c; }
int fflush(FILE* f) { (void)f; return 0; }
int fputs(const char* s, FILE* f) { (void)f; serial_write_str(s); return 0; }
int putc(int c, FILE* f) { (void)f; char b[2]={(char)c,0}; serial_write_str(b); return c; }

int sscanf(const char* str, const char* fmt, ...) {
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
                *va_arg(ap, unsigned*) = v; assigned++; break;
            }
            case 'd': case 'i': {
                while (*str == ' ' || *str == '\t') str++;
                int sign = 1, v = 0;
                if (*str == '-') { sign = -1; str++; } else if (*str == '+') str++;
                while (*str >= '0' && *str <= '9') v = v * 10 + (*str++ - '0');
                *va_arg(ap, int*) = sign * v; assigned++; break;
            }
            case 's': {
                char* out = va_arg(ap, char*);
                while (*str == ' ' || *str == '\t') str++;
                while (*str && *str != ' ' && *str != '\t' && *str != '\n') *out++ = *str++;
                *out = 0; assigned++; break;
            }
            case 'c': *va_arg(ap, char*) = *str++; assigned++; break;
            case '%': if (*str == '%') str++; break;
        }
        fmt++;
    }
    va_end(ap); return assigned;
}

// ============ RAND/QSort ============
static unsigned long _seed=1;
int rand(void){_seed=_seed*1103515245+12345;return (_seed>>16)&0x7FFF;}
void srand(unsigned s){_seed=s;}
static void sw(char*a,char*b,size_t n){while(n--){char t=*a;*a++=*b;*b++=t;}}
void qsort(void* base,size_t nm,size_t sz,int(*cmp)(const void*,const void*)){
    if(nm<2)return;char*b=base;char*piv=b+(nm/2)*sz;size_t i=0,j=nm-1;
    while(i<=j){while(cmp(b+i*sz,piv)<0)i++;while(cmp(b+j*sz,piv)>0)j--;if(i<=j){sw(b+i*sz,b+j*sz,sz);i++;if(j)j--;}}
    if(j+1>1)qsort(b,j+1,sz,cmp);if(nm>i)qsort(b+i*sz,nm-i,sz,cmp);
}

void exit(int c) { (void)c; doom_should_exit = 1; while (1) __asm__ volatile("hlt"); }
int system(const char* cmd) { (void)cmd; return -1; }

// ============ FAKE stdio на WAD в памяти ============
struct _dg_file { const uint8_t* data; size_t size; size_t pos; int used; };
static struct _dg_file g_files[4];

FILE* fopen(const char* name, const char* mode){
    (void)mode;
    if (strstr(name, "doom1.wad") || strstr(name, "DOOM1.WAD") || strstr(name, "doom.wad") || strstr(name, "DOOM.WAD")) {
        static int idx = 0; struct _dg_file* f = &g_files[idx]; idx = (idx + 1) % 4;
        f->data = doom1_wad_start; f->size = (size_t)(doom1_wad_end - doom1_wad_start); f->pos = 0; f->used = 1;
        return (FILE*)f;
    }
    return 0;
}
int fclose(FILE* fp){ if(fp){ struct _dg_file* f=(void*)fp; f->used=0; } return 0; }
size_t fread(void* buf, size_t sz, size_t n, FILE* fp){
    struct _dg_file* f=(void*)fp; size_t need = sz*n;
    if(f->pos + need > f->size) need = f->size - f->pos;
    memcpy(buf, f->data + f->pos, need); f->pos += need; return need / sz;
}
size_t fwrite(const void* b, size_t s, size_t n, FILE* f){(void)b;(void)s;(void)n;(void)f;return 0;}
int fseek(FILE* fp, long off, int whence){
    struct _dg_file* f=(void*)fp; long np = off;
    if(whence==SEEK_CUR) np = (long)f->pos + off; else if(whence==SEEK_END) np = (long)f->size + off;
    if(np < 0) np = 0; if((size_t)np > f->size) np = f->size;
    f->pos = (size_t)np; return 0;
}
long ftell(FILE* fp){ struct _dg_file* f=(void*)fp; return (long)f->pos; }
int fgetc(FILE* fp){ struct _dg_file* f=(void*)fp; if(f->pos>=f->size)return EOF; return f->data[f->pos++]; }
char* fgets(char* b, int n, FILE* fp){ int i=0; while(i<n-1){ int c=fgetc(fp); if(c==EOF)break; b[i++]=(char)c; if(c=='\n')break; } b[i]=0; return i?b:0; }
int fputc(int c, FILE* f){(void)c;(void)f;return 0;}

// ============ STUBS для Doom ============
int remove(const char* path) { (void)path; return 0; }
int rename(const char* a, const char* b) { (void)a; (void)b; return 0; }
int mkstemp(char* tmpl) { (void)tmpl; return -1; }
int unlink(const char* p) { (void)p; return 0; }
int access(const char* p, int m) { (void)p; (void)m; return -1; }
int stat(const char* p, struct stat* s) { (void)p; (void)s; errno = 2; return -1; }
int mkdir(const char* p, unsigned m) { (void)p; (void)m; return -1; }
FILE* fdopen(int fd, const char* m) { (void)fd; (void)m; return NULL; }
int close(int fd) { (void)fd; return 0; }