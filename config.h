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

// state bits to ignore when matching key or button events.
// use `xmodmap` to check your keyboard modifier map.
u32 IGNOREMOD = Mod2Mask;

// keymap
Button BTN_MAIN = {Button1};
Button BTN_SEL_CIRC = {Button3};
Button BTN_SCROLL_DRAG = {Button2};
Button BTN_SCROLL_UP = {Button4};
Button BTN_SCROLL_DOWN = {Button5};
Button BTN_SCROLL_LEFT = {Button4, ShiftMask};
Button BTN_SCROLL_RIGHT = {Button5, ShiftMask};
Button BTN_ZOOM_IN = {Button4, ControlMask};
Button BTN_ZOOM_OUT = {Button5, ControlMask};
Button BTN_COPY_SELECTION = {Button1, ShiftMask};  // by default area moves
// actions {allowed modes, {key, modifier mask}}
Action ACT_UNDO = {MF_Int, {XK_z, ControlMask}};
Action ACT_REVERT = {MF_Int, {XK_Z, ShiftMask | ControlMask}};
Action ACT_COPY_AREA = {MF_Int, {XK_c, ControlMask}};  // to clipboard
Action ACT_SWAP_COLOR = {MF_Int, {XK_x}};
Action ACT_ZOOM_IN = {MF_Int, {XK_equal, ControlMask}};
Action ACT_ZOOM_OUT = {MF_Int, {XK_minus, ControlMask}};
Action ACT_NEXT_COLOR = {MF_Int | MF_Color, {XK_Up}};
Action ACT_PREV_COLOR = {MF_Int | MF_Color, {XK_Down}};
Action ACT_SAVE_TO_FILE = {MF_Int | MF_Color, {XK_s, ControlMask}};
Action ACT_EXIT = {MF_Int | MF_Color, {XK_q}};
Action ACT_ADD_COLOR = {MF_Color, {XK_Up, ControlMask}};
Action ACT_TO_RIGHT_COL_DIGIT = {MF_Color, {XK_Right}};
Action ACT_TO_LEFT_COL_DIGIT = {MF_Color, {XK_Left}};
// mode switch
Action ACT_MODE_INTERACT = {
    MF_Color,  // XXX list all modes
    {XK_Escape}
};  // return to interact
Action ACT_MODE_COLOR = {MF_Int, {XK_c}};
Action ACT_MODE_CONSOLE = {MF_Int, {XK_colon, ShiftMask}};
// only in terminal mode
Key KEY_TERM_REQUEST_COMPLT = {XK_Tab};
Key KEY_TERM_NEXT_COMPLT = {XK_Tab};
Key KEY_TERM_APPLY_COMPLT = {XK_Return};
Key KEY_TERM_ERASE_CHAR = {XK_BackSpace};
Key KEY_TERM_RUN = {XK_Return};

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
