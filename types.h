#ifndef TYPES_H
#define TYPES_H

#ifdef USE_STD_INTTYPES

#include <inttypes.h>

typedef uint16_t u16;
typedef uint32_t u32;

#else

typedef unsigned short u16;
typedef unsigned int u32;

#endif

#endif