#include <X11/X.h>
#include <X11/extensions/Xrender.h>

#include "types.h"

char const title[] = "xpaint";

u32 const MAX_COLORS = 9;
u32 const TCS_NUM = 3;
char const FONT_NAME[] = "monospace:size=10";
// lag prevention. only one drag event per period will be done
u32 const DRAG_PERIOD_US = 10000;
i32 const PNG_DEFAULT_COMPRESSION = 8;
i32 const JPG_DEFAULT_QUALITY = 80;

XRenderColor const SCHEMES[SchmLast][2] = {
    // fg, bg (rgba premultiplied)
    [SchmNorm] =
        {{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0x1818, 0x1818, 0x1818, 0xFFFF}},
    [SchmFocus] = {{0xFFFF, 0, 0, 0xFFFF}, {0x9999, 0x1818, 0x1818, 0xFFFF}},
};

struct {
    u32 background_argb;
    Pair min_launch_size;
    Pair max_launch_size;
    i32 anchor_size;  // 0 to disable
} const WINDOW = {
    .background_argb = 0xFF181818,
    .min_launch_size = {350, 300},
    .max_launch_size = {1000, 1000},
    .anchor_size = 8
};

struct {
    Button btn_main;
    Button btn_sel_circ;
    Button btn_scroll_drag;
    Button btn_scroll_up;
    Button btn_scroll_down;
    Button btn_scroll_left;
    Button btn_scroll_right;
    Button btn_zoom_in;
    Button btn_zoom_out;
    Button btn_copy_selection;  // area moves by default
    // keys
    Key undo;
    Key copy_area;  // to clipboard
    Key swap_color;
    Key zoom_in;
    Key zoom_out;
    Key next_color;
    Key prev_color;
    Key save_to_file;
    Key exit;
    // modes
    Key mode_color;
    Key mode_console;
    Key mode_interact;
    // color mode
    Key add_color;
    Key to_right_col_digit;
    Key to_left_col_digit;
    // console mode
    Key request_completions;
    Key next_completion;
    Key apply_completion;
    Key erase_char;
    Key run;
} const KEYS = {
    .btn_main = {Button1},  // LMB
    .btn_sel_circ = {Button3},  // RMB
    .btn_scroll_drag = {Button2},  // MMB
    .btn_scroll_up = {Button4},  // Mouse scroll up
    .btn_scroll_down = {Button5},  // Mouse scroll down
    .btn_scroll_left = {Button4, ShiftMask},  // Mouse scroll up
    .btn_scroll_right = {Button5, ShiftMask},  // Mouse scroll down
    .btn_zoom_in = {Button4, ControlMask},
    .btn_zoom_out = {Button5, ControlMask},
    .btn_copy_selection = {Button1, ShiftMask},
    // keys
    .undo = {XK_z, ControlMask},
    .copy_area = {XK_c, ControlMask},
    .swap_color = {XK_x},
    .zoom_in = {XK_equal, ControlMask},
    .zoom_out = {XK_minus, ControlMask},
    .next_color = {XK_Up},
    .prev_color = {XK_Down},
    .save_to_file = {XK_s, ControlMask},
    .exit = {XK_q},
    // modes
    .mode_color = {XK_c},
    .mode_console = {XK_colon, ShiftMask},
    .mode_interact = {XK_Escape},
    // color mode
    .add_color = {XK_Up, ControlMask},
    .to_right_col_digit = {XK_Right},
    .to_left_col_digit = {XK_Left},
    // console mode
    .request_completions = {XK_Tab},
    .next_completion = {XK_Tab},
    .apply_completion = {XK_Return},
    .erase_char = {XK_BackSpace},
    .run = {XK_Return},
};

struct {
    u32 padding_bottom;
} const STATUSLINE = {
    .padding_bottom = 4,
};

struct {
    u32 outer_r_px;
    u32 inner_r_px;
    u32 line_w;
    i32 line_style;
    i32 cap_style;
    i32 join_style;
} const SELECTION_CIRCLE = {
    .outer_r_px = 225,
    .inner_r_px = 40,
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
    Bool draw_while_drag;
    u32 rect_argb;
    u32 drag_argb;
} const SELECTION_TOOL = {
    .line_w = 2,
    .line_style = LineOnOffDash,
    .cap_style = CapNotLast,
    .join_style = JoinMiter,
    .draw_while_drag = False,
    .rect_argb = 0xFF181818,
    .drag_argb = 0xFFE01818,
};

struct {
    u32 default_line_w;
} const TOOLS = {
    .default_line_w = 5,
};

struct {
    u32 background_argb;
    u32 default_width;
    u32 default_height;
    i32 min_zoom;
    i32 max_zoom;
} const CANVAS = {
    .background_argb = 0xFFAA0000,
    .default_width = 1000,
    .default_height = 700,
    .min_zoom = -10,
    .max_zoom = 30,  // at high values visual glitches appear
};
