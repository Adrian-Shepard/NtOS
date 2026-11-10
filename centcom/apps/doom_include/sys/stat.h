#pragma once
typedef unsigned int mode_t;
struct stat {
    mode_t st_mode;
    unsigned long st_size;
    int st_uid, st_gid;
};
#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
int stat(const char*, struct stat*);
int mkdir(const char*, mode_t);