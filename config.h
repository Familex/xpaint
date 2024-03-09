#include <X11/X.h>

#include "types.h"

char const title[] = "xpaint";

u32 const MAX_COLORS = 9;
u32 const TCS_NUM = 3;
char const FONT_NAME[] = "monospace:size=10";

struct {
    u32 background_rgb;
} const WINDOW = {
    .background_rgb = 0xFF181818,
};

struct {
    u32 background_argb;
    u32 font_argb;
    u32 strong_font_argb;
    u32 padding_bottom;
} const STATUSLINE = {
    .background_argb = 0xFF000000,
    .font_argb = 0xFFFFFFFF,
    .strong_font_argb = 0xFFFF0000,
    .padding_bottom = 4,
};

struct {
    u32 outer_r_px;
    u32 inner_r_px;
    u32 line_col_argb;
    u32 background_argb;
    u32 active_background_argb;
    u32 active_inner_background_argb;
    u32 line_w;
    i32 line_style;
    i32 cap_style;
    i32 join_style;
} const SELECTION_CIRCLE = {
    .outer_r_px = 225,
    .inner_r_px = 40,
    .line_col_argb = 0xFFAAAA00,
    .background_argb = 0xFF000000,  // hide png background
    .active_background_argb = 0xFFAAAA00,
    .active_inner_background_argb = 0xFF000000,
    .line_w = 2,
    .line_style = LineSolid,
    .cap_style = CapNotLast,
    .join_style = JoinMiter,
};

struct {
    u32 line_w;
    i32 line_style;
    i32 cap_style;
    i32 join_style;
    u32 drag_argb;
} const SELECTION_TOOL = {
    .line_w = 2,
    .line_style = LineOnOffDash,
    .cap_style = CapNotLast,
    .join_style = JoinMiter,
    .drag_argb = 0xFFFF0000,
};

struct {
    u32 drag_period_us;
} const PENCIL_TOOL = {
    .drag_period_us = 10000,
};

struct {
    u32 background_argb;
    u32 default_width;
    u32 default_height;
} const CANVAS = {
    .background_argb = 0xFFAA0000,
    .default_width = 500,
    .default_height = 800,
};
