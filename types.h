#ifndef XPAINT_TYPES_H__
#define XPAINT_TYPES_H__

#include <limits.h>
#include <stdint.h>

typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct {
    i32 x;
    i32 y;
} Pair;

#endif // XPAINT_TYPES_H__
