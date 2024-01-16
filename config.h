#include <X11/X.h>

#include "types.h"

char const title[] = "xpaint";

u32 const MAX_COLORS = 9;
u32 const TCS_NUM = 3;

struct {
    u32 background_rgb;
} const WINDOW = {
    .background_rgb = 0x181818,
};

struct {
    u32 background_rgb;
    u32 font_rgb;
    u32 strong_font_rgb;
    u32 height_px;  // FIXME
} const STATUSLINE = {
    .background_rgb = 0x000000,
    .font_rgb = 0xFFFFFF,
    .strong_font_rgb = 0xFF0000,
    .height_px = 10,
};

struct {
    u32 outer_r_px;
    u32 inner_r_px;
    u32 line_col_rgb;
    u32 background_rgb;
    u32 active_background_rgb;
    u32 active_inner_background_rgb;
    u32 line_w;
    i32 line_style;
    i32 cap_style;
    i32 join_style;
} const SELECTION_CIRCLE = {
    .outer_r_px = 225,
    .inner_r_px = 40,
    .line_col_rgb = 0xAAAA00,
    .background_rgb = 0x000000,  // hide png background
    .active_background_rgb = 0xAAAA00,
    .active_inner_background_rgb = 0x000000,
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
    u32 drag_color;
} const SELECTION_TOOL = {
    .line_w = 2,
    .line_style = LineOnOffDash,
    .cap_style = CapNotLast,
    .join_style = JoinMiter,
    .drag_color = 0xFF0000,
};

struct {
    u32 background_rgb;
    u32 default_width;
    u32 default_height;
} const CANVAS = {
    .background_rgb = 0xAA0000,
    .default_width = 500,
    .default_height = 800,
};
