#include <X11/X.h>
#include <X11/extensions/Xrender.h>

#include "types.h"

char const title[] = "xpaint";

// lag prevention
// only one drag event per period will be processed
u32 const DRAG_EVENT_PROC_PERIOD_US = 10000;
// update rate on screen scroll with mouse drag
u32 const MOUSE_SCROLL_UPDATE_PERIOD_US = 16000;

u32 const MAX_COLORS = 9;
u32 const TCS_NUM = 3;
char const FONT_NAME[] = "monospace:size=10";
i32 const PNG_DEFAULT_COMPRESSION = 8;
i32 const JPG_DEFAULT_QUALITY = 80;
double const CANVAS_ZOOM_SPEED = 1.2;  // must be > 1.0
// state bits to ignore when matching key or button events.
// use `xmodmap` to check your keyboard modifier map.
u32 const IGNOREMOD = Mod2Mask;

XRenderColor const SCHEMES[SchmLast][2] = {
    // fg, bg (rgba premultiplied)
    [SchmNorm] =
        {{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0x1818, 0x1818, 0x1818, 0xFFFF}},
    [SchmFocus] = {{0xFFFF, 0, 0, 0xFFFF}, {0x9999, 0x1818, 0x1818, 0xFFFF}},
};

// see types.h for definitions
SLModule const LEFT_MODULES[] = {
    {SLM_ToolCtx},
    {SLM_Mode},
    {SLM_Tool},
    {SLM_Text, .d.text = "| w: "},
    {SLM_ToolLineW},
};

SLModule const RIGHT_MODULES[] = {
    {SLM_ColorBox, .d.color_box_w = 24},
    {SLM_ColorName},
    {SLM_ColorList},
};

u32 const STATUSLINE_MODULE_SPACING_PX = 5;
u32 const STATUSLINE_MODULE_SPACING_SMALL_PX = STATUSLINE_MODULE_SPACING_PX / 2;
u32 const STATUSLINE_PADDING_BOTTOM = 4;

argb const WND_BACKGROUND = 0xFF181818;
Pair const WND_LAUNCH_MIN_SIZE = {350, 300};
Pair const WND_LAUNCH_MAX_SIZE = {1000, 1000};
i32 const WND_ANCHOR_CROSS_SIZE = 8;  // 0 to disable

// selection circle
u32 const SEL_CIRC_OUTER_R_PX = 225;
u32 const SEL_CIRC_INNER_R_PX = 40;
u32 const SEL_CIRC_LINE_W = 2;
i32 const SEL_CIRC_LINE_STYLE = LineSolid;

// selection tool
u32 const SEL_TOOL_LINE_W = 2;
argb const SEL_TOOL_COL = 0x80000000;
i32 const SEL_TOOL_LINE_STYLE = LineOnOffDash;
argb const SEL_TOOL_SELECTION_FG = 0xFF181818;

u32 const TOOLS_DEFAULT_LINE_W = 5;
u32 const TOOLS_BRUSH_DEFAULT_SPACING = 1;  // '1' to disable. must be >= 1

argb const CANVAS_BACKGROUND = 0xFFAA0000;
u32 const CANVAS_DEFAULT_WIDTH = 1000;
u32 const CANVAS_DEFAULT_HEIGHT = 700;
i32 const CANVAS_MIN_ZOOM = -10;
i32 const CANVAS_MAX_ZOOM = 30;  // at high values visual glitches appear

// ----- keymap. use ANY_MOD to ignore modifier, ANY_KEY to ignore key -----

Button const BTN_MAIN = {Button1, ANY_MOD};
Button const BTN_SEL_CIRC = {Button3};
Button const BTN_SCROLL_DRAG = {Button2};
Button const BTN_SCROLL_UP = {Button4};
Button const BTN_SCROLL_DOWN = {Button5};
Button const BTN_SCROLL_LEFT = {Button4, ShiftMask};
Button const BTN_SCROLL_RIGHT = {Button5, ShiftMask};
Button const BTN_ZOOM_IN = {Button4, ControlMask};
Button const BTN_ZOOM_OUT = {Button5, ControlMask};
// by default area moves
Button const BTN_COPY_SELECTION = {Button1, ShiftMask};
Button const BTN_TRANS_MOVE = {Button1};

// actions {allowed modes, {key, modifier mask}}
Action const ACT_UNDO = {MF_Int, {XK_z, ControlMask}};
Action const ACT_REVERT = {MF_Int, {XK_Z, ShiftMask | ControlMask}};
Action const ACT_COPY_AREA = {MF_Int, {XK_c, ControlMask}};  // to clipboard
Action const ACT_SWAP_COLOR = {MF_Int, {XK_x}};
Action const ACT_ZOOM_IN = {MF_Int, {XK_equal, ControlMask}};
Action const ACT_ZOOM_OUT = {MF_Int, {XK_minus, ControlMask}};
Action const ACT_NEXT_COLOR = {MF_Int | MF_Color, {XK_Up}};
Action const ACT_PREV_COLOR = {MF_Int | MF_Color, {XK_Down}};
Action const ACT_SAVE_TO_FILE = {MF_Int | MF_Color, {XK_s, ControlMask}};
Action const ACT_EXIT = {MF_Int | MF_Color, {XK_q}};
Action const ACT_ADD_COLOR = {MF_Color, {XK_Up, ControlMask}};
Action const ACT_TO_RIGHT_COL_DIGIT = {MF_Color, {XK_Right}};
Action const ACT_TO_LEFT_COL_DIGIT = {MF_Color, {XK_Left}};

// mode switch
Action const ACT_MODE_INTERACT = {
    MF_Color,  // XXX list all modes
    {XK_Escape}
};  // return to interact
Action const ACT_MODE_COLOR = {MF_Int, {XK_c}};
Action const ACT_MODE_CONSOLE = {MF_Int, {XK_colon, ShiftMask}};

// only in console mode
Key const KEY_CL_REQ_COMPLT = {XK_Tab};
Key const KEY_CL_NEXT_COMPLT = {XK_Tab};
Key const KEY_CL_APPLY_COMPLT = {XK_Return};
Key const KEY_CL_ERASE_CHAR = {XK_BackSpace};
Key const KEY_CL_RUN = {XK_Return};
