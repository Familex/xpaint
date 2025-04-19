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
char const UI_FONT_NAME[] = "monospace:size=10";
i32 const PNG_DEFAULT_COMPRESSION = 8;
i32 const JPG_DEFAULT_QUALITY = 80;
double const CANVAS_ZOOM_SPEED = 1.2;  // must be > 1.0
// state bits to ignore when matching key or button events.
// use `xmodmap` to check your keyboard modifier map.
u32 const IGNOREMOD = Mod2Mask;
u32 const AltMask = Mod1Mask;

XRenderColor const SCHEMES[SchmLast][2] = {
    // fg, bg (rgba premultiplied)
    [SchmNorm] = {{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0x1818, 0x1818, 0x1818, 0xFFFF}},
    [SchmFocus] = {{0xFFFF, 0, 0, 0xFFFF}, {0x9999, 0x1818, 0x1818, 0xFFFF}},
};

// see types.h for definitions
SLModule const LEFT_MODULES[] = {
    {SLM_ToolCtx, .d = {0}},
    {SLM_Mode, .d = {0}},
    {SLM_Tool, .d = {0}},
    {SLM_Text, .d.text = "|"},
    {SLM_ToolSettings, .d = {0}},
};

SLModule const RIGHT_MODULES[] = {
    {SLM_ColorBox, .d.color_box_w = 24},
    {SLM_ColorName, .d = {0}},
    {SLM_ColorList, .d = {0}},
};

u32 const STATUSLINE_MODULE_SPACING_PX = 5;
u32 const STATUSLINE_MODULE_SPACING_SMALL_PX = STATUSLINE_MODULE_SPACING_PX / 2;
u32 const STATUSLINE_PADDING_BOTTOM = 4;
u32 const STATUSLINE_COMPLS_LIST_MAX = 10;
u32 const STATUSLINE_COMPLS_DESRCIPTION_PADDING_PX = 50;

argb const WND_BACKGROUND = 0xFF181818;
Pair const WND_LAUNCH_MIN_SIZE = {350, 300};
Pair const WND_LAUNCH_MAX_SIZE = {1000, 1000};
i32 const WND_ANCHOR_CROSS_SIZE = 8;  // 0 to disable

// selection circle
u32 const SEL_CIRC_OUTER_R_PX = 225;
u32 const SEL_CIRC_INNER_R_PX = 40;
u32 const SEL_CIRC_LINE_W = 2;
i32 const SEL_CIRC_LINE_STYLE = LineSolid;
u32 const SEL_CIRC_COLOR_ITEMS = 100;

// selection tool
u32 const SEL_TOOL_LINE_W = 2;
argb const SEL_TOOL_COL = 0x80000000;
i32 const SEL_TOOL_LINE_STYLE = LineOnOffDash;
argb const SEL_TOOL_SELECTION_FG = 0xFF181818;

// text tool
char const* const TEXT_TOOL_DEFAULT_FONT = "monospace-24";

u32 const TOOLS_DEFAULT_LINE_W = 5;
u32 const TOOLS_BRUSH_DEFAULT_SPACING = 1;  // '1' to disable. must be >= 1

double const TFM_MODE_ROTATE_SENSITIVITY = 0.01;

argb const CANVAS_BACKGROUND = 0xFFAA0000;
u32 const CANVAS_DEFAULT_WIDTH = 1000;
u32 const CANVAS_DEFAULT_HEIGHT = 700;
i32 const CANVAS_MIN_ZOOM = -10;
i32 const CANVAS_MAX_ZOOM = 30;  // at high values visual glitches appear

Bool const CONSOLE_AUTO_COMPLETIONS = True;

// ---------------------------- keymap ---------------------------------------
// use `xev` command to check keycodes

Button const BTN_MAIN = {Button1, ANY_MOD};
Button const BTN_SEL_CIRC = {Button3, NO_MOD};
Button const BTN_SEL_CIRC_ALTERNATIVE = {Button3, AltMask};
Button const BTN_CANVAS_RESIZE = {Button3, ControlMask};
Button const BTN_SCROLL_DRAG = {Button2, NO_MOD};
Button const BTN_SCROLL_UP = {Button4, NO_MOD};
Button const BTN_SCROLL_DOWN = {Button5, NO_MOD};
Button const BTN_SCROLL_LEFT = {Button4, ShiftMask};
Button const BTN_SCROLL_RIGHT = {Button5, ShiftMask};
Button const BTN_ZOOM_IN = {Button4, ControlMask};
Button const BTN_ZOOM_OUT = {Button5, ControlMask};
// by default area moves
Button const BTN_COPY_SELECTION = {Button1, ShiftMask};
Button const BTN_TRANS_MOVE = {Button1, NO_MOD};
Button const BTN_TRANS_MOVE_LOCK = {Button1, ShiftMask};
Button const BTN_TRANS_SCALE = {Button3, AltMask};
Button const BTN_TRANS_SCALE_UNIFORM = {Button3, AltMask | ShiftMask};
Button const BTN_TRANS_ROTATE = {Button3, ControlMask};
Button const BTN_TRANS_ROTATE_SNAP = {Button3, ControlMask | ShiftMask};

// actions {allowed modes, {key, modifier mask}}
Action const ACT_UNDO = {MF_Int, {XK_z, ControlMask}};
Action const ACT_REVERT = {MF_Int, {XK_Z, ShiftMask | ControlMask}};
Action const ACT_COPY_AREA = {ANY_MODE, {XK_c, ControlMask}};  // to clipboard
Action const ACT_PASTE_IMAGE = {ANY_MODE, {XK_v, ControlMask}};
Action const ACT_SWAP_COLOR = {MF_Int, {XK_x, NO_MOD}};
Action const ACT_ZOOM_IN = {ANY_MODE, {XK_equal, ControlMask}};
Action const ACT_ZOOM_OUT = {ANY_MODE, {XK_minus, ControlMask}};
Action const ACT_NEXT_COLOR = {MF_Int | MF_Color, {XK_Up, NO_MOD}};
Action const ACT_PREV_COLOR = {MF_Int | MF_Color, {XK_Down, NO_MOD}};
Action const ACT_SAVE_TO_FILE = {MF_Int | MF_Color, {XK_s, ControlMask}};
Action const ACT_EXIT = {ANY_MODE, {XK_q, NO_MOD}};
Action const ACT_ADD_COLOR = {MF_Color, {XK_Up, ControlMask}};
Action const ACT_TO_RIGHT_COL_DIGIT = {MF_Color, {XK_Right, NO_MOD}};
Action const ACT_TO_LEFT_COL_DIGIT = {MF_Color, {XK_Left, NO_MOD}};

// mode switch
Action const ACT_MODE_INTERACT = {ANY_MODE, {XK_Escape, NO_MOD}};  // return to interact
Action const ACT_MODE_COLOR = {MF_Int, {XK_c, NO_MOD}};
Action const ACT_MODE_CONSOLE = {MF_Int, {XK_colon, ShiftMask}};

// only in text mode
Key const KEY_TX_CONFIRM = {XK_Return, NO_MOD};  // typing '\n' not supported by implementation
Key const KEY_TX_MODE_INTERACT = ACT_MODE_INTERACT.key;
Key const KEY_TX_PASTE_TEXT = ACT_PASTE_IMAGE.key;
Key const KEY_TX_ERASE_CHAR = {XK_BackSpace, NO_MOD};
Key const KEY_TX_ERASE_ALL = {XK_BackSpace, ControlMask};

// only in console mode
Key const KEY_CL_REQ_COMPLT = {NO_KEY, NO_MOD};
Key const KEY_CL_NEXT_COMPLT = {XK_Tab, NO_MOD};
Key const KEY_CL_PREV_COMPLT = {XK_ISO_Left_Tab, ShiftMask};
Key const KEY_CL_APPLY_COMPLT = {XK_Return, NO_MOD};
Key const KEY_CL_ERASE_CHAR = KEY_TX_ERASE_CHAR;
Key const KEY_CL_ERASE_ALL = KEY_TX_ERASE_ALL;
Key const KEY_CL_RUN = {XK_Return, NO_MOD};
Key const KEY_CL_CLIPBOARD_PASTE = ACT_PASTE_IMAGE.key;
Key const KEY_CL_MODE_INTERACT = ACT_MODE_INTERACT.key;
