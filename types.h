#ifndef XPAINT_TYPES_H__
#define XPAINT_TYPES_H__

#include <stddef.h>
#include <stdint.h>

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t usize;

typedef struct {
    i32 x;
    i32 y;
} Pair;

enum Schm {
    SchmNorm,
    SchmFocus,
    SchmLast,
};

#endif  // XPAINT_TYPES_H__
