#ifndef XPAINT_TYPES_H__
#define XPAINT_TYPES_H__

#include <X11/X.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define ANY_MOD UINT_MAX
#define ANY_KEY UINT_MAX
// default value for signed integers
#define NIL     (-1)
#define PNIL    ((Pair) {NIL, NIL})
#define RNIL \
    ((Rect) {.l = INT32_MAX, .t = INT32_MAX, .r = INT32_MIN, .b = INT32_MIN})

#define IS_RNIL(p_rect) \
    ((p_rect.l) == INT32_MAX && (p_rect.t) == INT32_MAX \
     && (p_rect.r) == INT32_MIN && (p_rect.b) == INT32_MIN)

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t usize;

typedef u32 argb;

typedef struct {
    i32 x;
    i32 y;
} Pair;

typedef struct {
    i32 l;
    i32 t;
    i32 r;
    i32 b;
} Rect;

typedef struct {
    double x;
    double y;
} DPt;

typedef struct {
    KeySym sym;
    u32 mask;
} Key;

typedef struct {
    u32 button;
    u32 mask;
} Button;

enum Schm {
    SchmNorm,
    SchmFocus,
    SchmLast,
};

typedef struct {
    enum {
        MF_Int = 0x1,  // interact
        MF_Color = 0x2,  // color
        // MF_Term managed manually because can use any key
    } mode;
    Key key;
} Action;

#endif  // XPAINT_TYPES_H__
