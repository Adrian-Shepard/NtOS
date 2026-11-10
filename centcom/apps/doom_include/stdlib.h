#pragma once
#include <stddef.h>

void* doom_malloc(size_t);
void  doom_free(void*);
void* doom_calloc(size_t, size_t);
void* doom_realloc(void*, size_t);

int   atoi(const char*);
double atof(const char*);
int   rand(void);
void  srand(unsigned);
void  qsort(void*, size_t, size_t, int (*)(const void*, const void*));
void  exit(int);
int   abs(int);
long  labs(long);
int   remove(const char*);
int   rename(const char*, const char*);
int   mkstemp(char*);
int   system(const char*);

// Перенаправляем стандартные вызовы DOOM на нашу безопасную кучу
#define malloc  doom_malloc
#define free    doom_free
#define calloc  doom_calloc
#define realloc doom_realloc

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1