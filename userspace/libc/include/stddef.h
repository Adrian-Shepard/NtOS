#pragma once

typedef unsigned long size_t;
typedef long          ssize_t;

#define NULL ((void*)0)
#define offsetof(type, member) __builtin_offsetof(type, member)