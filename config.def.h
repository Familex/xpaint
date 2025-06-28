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

struct Item const ALTERNATIVE_SEL_CIRC_ITEMS[] = {
    {.arg.tool = Tool_Pencil, .on_select = sel_circ_on_select_tool, .desc = "tool: pencil"},
    {.arg.col = 0xFF000000, .on_select = sel_circ_on_select_col, .desc = "col: black"},
    {.arg.col = 0xFFFFFFFF, .on_select = sel_circ_on_select_col, .desc = "col: white"},
    {.arg.num = 25, .on_select = sel_circ_on_select_set_linew, .desc = "line_w: 25"},
    {.arg.num = 10, .on_select = sel_circ_on_select_set_linew, .desc = "line_w: 10"},
    {.arg.num = 5, .on_select = sel_circ_on_select_set_linew, .desc = "line_w: 5"},
    {.arg.num = 1, .on_select = sel_circ_on_select_set_linew, .desc = "line_w: 1"},
};

argb const COLOR_LIST_DEFAULT[] = {
    0xFFFFFF00,
    0xFFFF8000,
    0xFFFF00FF,
    0xFFFFE8BB,
    0xFF000000,
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
u32 const SEL_CIRC_ITEM_ICON_MARGIN_PX = 5;

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

argb const CANVAS_BACKGROUND = 0xFF000000;
u32 const CANVAS_DEFAULT_WIDTH = 1000;
u32 const CANVAS_DEFAULT_HEIGHT = 700;
i32 const CANVAS_MIN_ZOOM = -10;
i32 const CANVAS_MAX_ZOOM = 22;  // at high values visual glitches appear

Bool const CONSOLE_AUTO_COMPLETIONS = True;

// ---------------------------- keymap ---------------------------------------
// use `xev` command to check keycodes

Button const BTN_MAIN[] = {{Button1, ANY_MOD}};
Button const BTN_MAIN_ALTERNATIVE[] = {{Button1, AltMask}};
Button const BTN_SEL_CIRC[] = {{Button3, NO_MOD}};
Button const BTN_SEL_CIRC_ALTERNATIVE[] = {{Button3, AltMask}};
Button const BTN_CANVAS_RESIZE[] = {{Button3, ControlMask}};
Button const BTN_SCROLL_DRAG[] = {{Button2, NO_MOD}};
Button const BTN_SCROLL_UP[] = {{Button4, ControlMask}};
Button const BTN_SCROLL_DOWN[] = {{Button5, ControlMask}};
Button const BTN_SCROLL_LEFT[] = {{Button4, ControlMask | ShiftMask}, {Button6, ControlMask}};
Button const BTN_SCROLL_RIGHT[] = {{Button5, ControlMask | ShiftMask}, {Button7, ControlMask}};
Button const BTN_ZOOM_IN[] = {{Button4, NO_MOD}};
Button const BTN_ZOOM_OUT[] = {{Button5, NO_MOD}};
// by default area moves
Button const BTN_COPY_SELECTION[] = {{Button1, ShiftMask}};
Button const BTN_TRANS_MOVE[] = {{Button1, NO_MOD}};
Button const BTN_TRANS_MOVE_LOCK[] = {{Button1, ShiftMask}};
Button const BTN_TRANS_SCALE[] = {{Button3, AltMask}};
Button const BTN_TRANS_SCALE_UNIFORM[] = {{Button3, AltMask | ShiftMask}};
Button const BTN_TRANS_ROTATE[] = {{Button3, ControlMask}};
Button const BTN_TRANS_ROTATE_SNAP[] = {{Button3, ControlMask | ShiftMask}};

// actions {allowed modes, {key, modifier mask}}
Key const ACT_UNDO[] = {{XK_z, ControlMask}};
Key const ACT_REVERT[] = {{XK_Z, ShiftMask | ControlMask}};
Key const ACT_COPY_AREA[] = {{XK_c, ControlMask}};  // to clipboard
Key const ACT_PASTE_IMAGE[] = {{XK_v, ControlMask}};
Key const ACT_SWAP_COLOR[] = {{XK_x, NO_MOD}};
Key const ACT_ZOOM_IN[] = {{XK_equal, ControlMask}};
Key const ACT_ZOOM_OUT[] = {{XK_minus, ControlMask}};
Key const ACT_NEXT_COLOR[] = {{XK_Up, NO_MOD}};
Key const ACT_PREV_COLOR[] = {{XK_Down, NO_MOD}};
Key const ACT_SAVE_TO_FILE[] = {{XK_s, ControlMask}};
Key const ACT_EXIT[] = {{XK_q, NO_MOD}};
Key const ACT_ADD_COLOR[] = {{XK_Up, ControlMask}};
Key const ACT_TO_RIGHT_COL_DIGIT[] = {{XK_Right, NO_MOD}};
Key const ACT_TO_LEFT_COL_DIGIT[] = {{XK_Left, NO_MOD}};

// mode switch
Key const ACT_MODE_INTERACT[] = {{XK_Escape, NO_MOD}};  // return to interact
Key const ACT_MODE_COLOR[] = {{XK_c, NO_MOD}};
Key const ACT_MODE_CONSOLE[] = {{XK_colon, ShiftMask}};

// only in text mode
Key const KEY_TX_CONFIRM[] = {{XK_Return, NO_MOD}};  // typing '\n' not supported by implementation
Key const KEY_TX_MODE_INTERACT[] = {{XK_Escape, NO_MOD}};
Key const KEY_TX_PASTE_TEXT[] = {{XK_v, ControlMask}};
Key const KEY_TX_ERASE_CHAR[] = {{XK_BackSpace, NO_MOD}};
Key const KEY_TX_ERASE_ALL[] = {{XK_BackSpace, ControlMask}};

// only in console mode
Key const KEY_CL_REQ_COMPLT[] = {{NO_KEY, NO_MOD}};
Key const KEY_CL_NEXT_COMPLT[] = {{XK_Tab, NO_MOD}};
Key const KEY_CL_PREV_COMPLT[] = {{XK_ISO_Left_Tab, ShiftMask}};
Key const KEY_CL_APPLY_COMPLT[] = {{XK_Return, NO_MOD}};
Key const KEY_CL_ERASE_CHAR[] = {{XK_BackSpace, NO_MOD}};
Key const KEY_CL_ERASE_ALL[] = {{XK_BackSpace, ControlMask}};
Key const KEY_CL_RUN[] = {{XK_Return, NO_MOD}};
Key const KEY_CL_CLIPBOARD_PASTE[] = {{XK_v, ControlMask}};
Key const KEY_CL_MODE_INTERACT[] = {{XK_v, ControlMask}};
