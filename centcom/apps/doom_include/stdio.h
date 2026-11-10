#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef struct _dg_file FILE;
FILE* fopen(const char*, const char*);
int   fclose(FILE*);
size_t fread(void*, size_t, size_t, FILE*);
size_t fwrite(const void*, size_t, size_t, FILE*);
int   fseek(FILE*, long, int);
long  ftell(FILE*);
int   fgetc(FILE*);
char* fgets(char*, int, FILE*);
int   fputc(int, FILE*);
int   fprintf(FILE*, const char*, ...);
int   printf(const char*, ...);
int   sprintf(char*, const char*, ...);
int   snprintf(char*, size_t, const char*, ...);
int   vsnprintf(char*, size_t, const char*, va_list);
int   remove(const char*);
int   rename(const char*, const char*);
int   mkstemp(char*);
int fflush(FILE*);
int puts(const char*);
int putchar(int);
int vfprintf(FILE*, const char*, va_list);
int fputs(const char*, FILE*);
int putc(int, FILE*);
int sscanf(const char*, const char*, ...);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
#define stderr ((FILE*)2)
#define stdout ((FILE*)1)
#define stdin  ((FILE*)0)