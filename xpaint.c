#include <X11/Xatom.h>  // XA_*
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xdbe.h>  // back buffer
#include <X11/extensions/Xrender.h>
#include <X11/extensions/sync.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

// libs
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#define INCBIN_PREFIX
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "lib/incbin.h"
#define STB_DS_IMPLEMENTATION
#include "lib/stb_ds.h"
#undef STB_DS_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "lib/stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#pragma GCC diagnostic pop

/*
 * -opt vars are nullable (optional)
 * free -dyn vars with 'free' function
 * free -arr vars with 'arrfree' function
 * free -imdyn vars with 'stbi_image_free' function
 * free -xdyn vars with 'XFree' function
 * structs with t and d fields are tagged unions
 */

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t usize;

// embedded data
INCBIN(u8, pic_tool_fill, "res/tool-fill.png");
INCBIN(u8, pic_tool_pencil, "res/tool-pencil.png");
INCBIN(u8, pic_tool_picker, "res/tool-picker.png");
INCBIN(u8, pic_tool_select, "res/tool-select.png");
INCBIN(u8, pic_tool_brush, "res/tool-brush.png");
INCBIN(u8, pic_tool_figure, "res/tool-figure.png");
INCBIN(u8, pic_tool_text, "res/tool-text.png");
INCBIN(u8, pic_fig_rect, "res/figure-rectangle.png");
INCBIN(u8, pic_fig_circ, "res/figure-circle.png");
INCBIN(u8, pic_fig_tri, "res/figure-triangle.png");
INCBIN(u8, pic_fig_fill_on, "res/figure-fill-on.png");
INCBIN(u8, pic_fig_fill_off, "res/figure-fill-off.png");

#define MAX(A, B)         ((A) > (B) ? (A) : (B))
#define MIN(A, B)         ((A) < (B) ? (A) : (B))
#define CLAMP(X, L, H)    (((X) < (L)) ? (L) : ((X) > (H)) ? (H) : (X))
#define LENGTH(X)         (sizeof(X) / sizeof(X)[0])
#define BETWEEN(X, A, B)  ((A) <= (X) && (X) <= (B))
#define COALESCE(A, B)    ((A) ? (A) : (B))
#define UNREACHABLE()     __builtin_unreachable()
#define IS_PNIL(p_pair)   ((p_pair).x == NIL && (p_pair).y == NIL)
#define PAIR_EQ(p_a, p_b) ((p_a).x == (p_b).x && (p_a).y == (p_b).y)
#define IS_RNIL(p_rect) \
    (((p_rect).l) == INT32_MAX && ((p_rect).t) == INT32_MAX && ((p_rect).r) == INT32_MIN && ((p_rect).b) == INT32_MIN)

enum { NO_MOD = 0 };
#define ANY_MOD UINT_MAX
#define NO_KEY  XK_VoidSymbol
#define ANY_KEY UINT_MAX
enum { NO_MODE = 0 };
#define ANY_MODE         UINT_MAX
// default value for signed integers
#define NIL              (-1)
#define PNIL             ((Pair) {NIL, NIL})
#define RNIL             ((Rect) {.l = INT32_MAX, .t = INT32_MAX, .r = INT32_MIN, .b = INT32_MIN})
#define PI               (3.141)
// only one one-byte symbol allowed
#define ARGB_ALPHA       ((argb)(0xFF000000))
#define CL_DELIM         " "
#define IOCTX_STDIO_STR  "-"
// adjust brush line width to pencil's line width
// XXX kinda works only for small values of line_w
#define BRUSH_LINE_W_MOD 4.0
#define TEXT_FONT_PROMPT "font: "
#define TEXT_MODE_PROMPT "text: "
#define CL_CMD_PROMPT    ":"

#define CURR_TC(p_ctx)     ((p_ctx)->tcarr[(p_ctx)->curr_tc])
// XXX workaround
#define COL_FG(p_dc, p_sc) ((p_dc)->schemes_dyn[(p_sc)].fg.pixel | 0xFF000000)
#define COL_BG(p_dc, p_sc) ((p_dc)->schemes_dyn[(p_sc)].bg.pixel | 0xFF000000)
#define OVERLAY_TRANSFORM(p_mode) \
    ((p_mode)->t != InputT_Transform ? TRANSFORM_DEFAULT : trans_add((p_mode)->d.trans.curr, (p_mode)->d.trans.acc))
#define TC_IS_DRAWER(p_tc) ((p_tc)->t == Tool_Pencil || (p_tc)->t == Tool_Brush)
#define ZOOM_C(p_dc)       (pow(CANVAS_ZOOM_SPEED, (double)(p_dc)->cv.zoom))
#define TRANSFORM_DEFAULT  ((Transform) {.scale = {1.0, 1.0}})

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
        SLM_Spacer,  // static spacer
        SLM_Text,  // static text
        SLM_ToolCtx,  // list of tool contexts
        SLM_Mode,  // current mode
        SLM_Tool,  // current tool
        SLM_ToolSettings,  // current tool settings
        SLM_ColorBox,  // rectangle filled with current color
        SLM_ColorName,  // name of current color
        SLM_ColorList,  // current index and size of color list
    } t;  // type
    union {
        u32 spacer;  // for SLM_Spacer
        char const* text;  // for SLM_Text
        u32 color_box_w;  // for SLM_ColorBox
    } d;  // data for corresponding type
} SLModule;  // status line modules

typedef struct {
    enum {
        MF_Int = 0x1,  // interact
        MF_Color = 0x2,  // color
        MF_Trans = 0x4,  // transform
        // MF_Term managed manually because can use any key
    } mode;
    Key key;
} Action;

enum {
    A_Cardinal,
    A_Clipboard,
    A_Targets,
    A_Utf8string,
    A_ImagePng,
    A_TextUriList,
    A_XSelData,
    A_WmProtocols,
    A_WmDeleteWindow,
    A_NetWmSyncRequest,
    A_NetWmSyncRequestCounter,
    A_XDndAware,
    A_XDndPosition,
    A_XDndSelection,
    A_XDndStatus,
    A_XDndActionCopy,
    A_XDndDrop,
    A_Last,
};

enum Icon {
    I_None,
    I_Select,
    I_Pencil,
    I_Fill,
    I_Picker,
    I_Brush,
    I_Figure,
    I_Text,
    I_FigRect,
    I_FigCirc,
    I_FigTri,
    I_FigFillOn,
    I_FigFillOff,
    I_Last,
};

struct IconData {
    u8 const* data;
    usize len;
};

struct Image {
    XImage* im;
    enum ImageType {
        IMT_Png,
        IMT_Jpg,
        IMT_Unknown,
    } type;
};

typedef struct {
    Pair move;
    DPt scale;  // (1, 1) for no scale change
    double rotate;
} Transform;

struct Ctx;
struct DrawCtx;
struct ToolCtx;
union SCI_Arg;

typedef void (*draw_fn)(struct Ctx* ctx, Pair p);

typedef u8 (*circle_get_alpha_fn)(
    u32 line_w,
    double circle_radius,
    Pair p  // point relative circle center
);

typedef enum {
    HR_Quit,
    HR_Ok,
} HdlrResult;

struct Ctx {
    struct DrawCtx {
        // readonly outside setup and cleanup functions
        struct System {
            XRenderPictFormat* xrnd_pic_format;
            XVisualInfo vinfo;
            XIM xim;
            XIC xic;
            Colormap colmap;
        } sys;

        Display* dp;
        GC gc;
        GC screen_gc;
        Window window;
        u32 width;
        u32 height;
        XdbeBackBuffer back_buffer;  // double buffering
        struct Canvas {
            XImage* im;
            enum ImageType type;
            i32 zoom;  // 0 == no zoom
            Pair scroll;
        } cv;
        XftFont* fnt;
        struct Scheme {
            XftColor fg;
            XftColor bg;
        }* schemes_dyn;  // must be len of SchmLast
        struct Cache {
            Pair dims;  // to validate pm and overlay

            Pixmap pm;  // pixel buffer to update screen
            Pixmap overlay;  // extra pixmap for overlay
        } cache;
    } dc;

    struct Input {
        struct CursorState {
            enum CursorStateTag {
                CS_None,
                CS_Hold,
                CS_Drag,
            } state;

            Button btn;  // invalid if state == CS_None
            Pair pos;  // invalid if state == CS_None
        } c;

        Pair prev_c;  // last cursor position

        u64 last_proc_drag_ev_us;  // optimization to reduce drag events
        Pair anchor;  // cursor position of last processed drawing tool event

        i32 png_compression_level;  // FIXME find better place
        i32 jpg_quality_level;  // FIXME find better place

        // drawn on top of canvas
        // clears before *on_press callbacks
        // dumps to main canvas after *on_release callbacks
        struct InputOverlay {
            XImage* im;
            Rect rect;  // bounding box of actual content
        } ovr;

        // tracks damage to overlay from _on_press to _on_release.
        Rect damage;
        // parts of overlay and canvas to redraw in update_screen
        // stores last and previous damages to handle full screen clears, e.g. figure tool or selection tool
        Rect redraw_track[2];

        struct InputMode {
            enum InputTag {
                InputT_Interact,
                InputT_Color,
                InputT_Console,
                InputT_Transform,
                InputT_Text,
            } t;
            union {
                struct InputColorData {
                    u32 current_digit;
                } col;
                struct InputConsoleData {
                    char* cmdarr;
                    struct ComplsItem {
                        char* val_dyn;
                        char* descr_optdyn;
                    }* compls_arr;
                    usize compls_curr;
                    // Automatic delimeter append breaks paths
                    Bool dont_append_delimeter_after_apply;
                } cl;
                struct InputTransformData {
                    Transform acc;  // accumulated
                    Transform curr;  // current mouse drag
                } trans;
                struct InputTextData {
                    char* textarr;
                    struct ToolTextData {
                        Pair lb_corner;
                    } tool_data;  // copied from text tool on mouse release
                } text;
            } d;
        } mode;
    } input;

    struct ToolCtx {
        // returns overlay damage
        Rect (*on_press)(struct Ctx*, XButtonPressedEvent const*);
        Rect (*on_release)(struct Ctx*, XButtonReleasedEvent const*);
        Rect (*on_drag)(struct Ctx*, XMotionEvent const*);
        Rect (*on_move)(struct Ctx*, XMotionEvent const*);

        argb* colarr;
        u32 curr_col;
        u32 prev_col;
        u32 line_w;
        XftFont* text_font;

        enum ToolTag {
            Tool_Selection,
            Tool_Pencil,
            Tool_Fill,
            Tool_Picker,
            Tool_Brush,
            Tool_Figure,
            Tool_Text,
        } t;
        union ToolData {
            // Tool_Pencil | Tool_Brush
            struct DrawerData {
                enum DrawerShape {
                    DS_Circle,
                    DS_Square,
                } shape;
                u32 spacing;
            } drawer;
            struct FigureData {
                enum FigureType {
                    Figure_Circle,
                    Figure_Rectangle,
                    Figure_Triangle,
                } curr;
                Bool fill;
            } fig;
            struct ToolTextData text;
        } d;
    }* tcarr;
    u32 curr_tc;

    struct HistItem {
        Pair pivot;  // top left corner position
        XImage* patch;  // changed canvas part
    } *hist_prevarr, *hist_nextarr;

    struct SelectionCircle {
        i32 x;
        i32 y;
        Bool draw_separators;
        struct Item {
            void (*on_select)(struct Ctx* ctx, union SCI_Arg arg);
            union SCI_Arg {
                enum ToolTag tool;
                enum FigureType figure;
                argb col;
                usize num;
                void* custom;
            } arg;

            enum Icon icon;  // option icon
            char const* desc;  // option description
            argb col_outer;
            argb col_inner;
        }* items_arr;
    } sc;

    struct StateXSync {
        XSyncCounter counter;
        XSyncValue last_request_value;
    } xsync;

    struct SelectionBuffer {
        XImage* im;
    } sel_buf;

    struct IOCtx {
        enum {
            IO_None,
            IO_File,
            IO_Stdio,
        } t;
        union {
            struct {
                char* path_dyn;
            } file;
        } d;
    } inp, out;
};

struct ClCommand {
    enum ClCTag {
        ClC_Echo = 0,
        ClC_Set,
        ClC_Exit,
        ClC_Save,
        ClC_W,
        ClC_WQ,
        ClC_Load,
        ClC_Last,
    } t;
    union ClCData {
        struct ClCDSet {
            enum ClCDSTag {
                ClCDS_LineW = 0,
                ClCDS_Col,
                ClCDS_UiFont,
                ClCDS_TextFont,
                ClCDS_Inp,
                ClCDS_Out,
                ClCDS_PngCompression,
                ClCDS_JpgQuality,
                ClCDS_Spacing,
                ClCDS_Last,
            } t;
            union ClCDSData {
                struct ClCDSDLineW {
                    u32 value;
                } line_w;
                struct ClCDSDCol {
                    argb v;
                } col;
                struct ClCDSDFont {
                    char* name_dyn;
                } ui_font, text_font;
                struct ClCDSDInp {
                    char* path_dyn;
                } inp;
                struct ClCDSDOut {
                    char* path_dyn;
                } out;
                struct ClCDSDPngCpr {
                    i32 compression;
                } png_cpr;
                struct ClCDSDJpgQlt {
                    i32 quality;
                } jpg_qlt;
                struct ClCDSDSpacing {
                    u32 val;
                } spacing;
            } d;
        } set;
        struct ClCDEcho {
            char* msg_dyn;
        } echo;
        struct ClCDSave {
            enum ClCDSv {
                ClCDSv_Png = 0,
                ClCDSv_Jpg,
                ClCDSv_Last,
            } im_type;
            char* path_dyn;
        } save;
        struct ClCDLoad {
            char* path_dyn;
        } load;
    } d;
};

typedef struct {
    enum {
        ClCPrc_Msg = 0x1,  // wants to show message
        ClCPrc_Exit = 0x2,  // wants to exit application
    } bit_status;
    char* msg_dyn;  // NULL if not PCCR_MSG
} ClCPrcResult;

typedef struct {
    enum {
        ClCPrs_Ok,
        ClCPrs_ENoArg,
        ClCPrs_EInvArg,  // invalid
    } t;
    union {
        struct ClCommand ok;
        struct {
            char* arg_desc_dyn;
            char* context_optdyn;
        } noarg;
        struct {
            char* arg_dyn;
            char* error_dyn;
            char* context_optdyn;
        } invarg;
    } d;
} ClCPrsResult;

// clang-format off
__attribute__((noreturn)) static void die(char const* errstr, ...);
static void trace(char const* fmt, ...);
static void* ecalloc(u32 n, u32 size);
static u32 digit_count(u32 number);
static void arrpoputf8(char const* strarr);
static usize first_dismatch(char const* restrict s1, char const* restrict s2);
static struct IconData get_icon_data(enum Icon icon);
static double brush_ease(double v);
static Bool state_match(u32 a, u32 b);
static Button get_btn(XButtonEvent const* e);
static Bool btn_eq(Button a, Button b);
static Bool key_eq(Key a, Key b);
static Bool can_action(struct Input const* input, Key curr_key, Action act);
static char* uri_to_path(char const* uri);

static Rect rect_expand(Rect a, Rect b);
static Pair rect_dims(Rect a);

static Transform trans_add(Transform a, Transform b);
static XTransform xtrans_overlay_transform_mode(struct Input const* input);
static XTransform xtrans_scale(double x, double y);
static XTransform xtrans_move(double x, double y);
static XTransform xtrans_rotate(double a);
static XTransform xtrans_from_trans(Transform trans);
static XTransform xtrans_mult(XTransform a, XTransform b); // transformations are applied from right to left
static XTransform xtrans_invert(XTransform a);

static void xwindow_set_cardinal(Display* dp, Window window, Atom key, u32 value);

// needs to be 'free'd after use
static char* str_new(char const* fmt, ...);
static char* str_new_va(char const* fmt, va_list args);
static void str_free(char** str_dyn);

static struct ToolCtx tc_new(struct DrawCtx* dc);
static void tc_set_curr_col_num(struct ToolCtx* tc, u32 value);
static argb* tc_curr_col(struct ToolCtx* tc);
static void tc_set_tool(struct ToolCtx* tc, enum ToolTag type);
static char const* tc_get_tool_name(struct ToolCtx const* tc);
static void tc_free(Display* dp, struct ToolCtx* tc);

// free: with `free` and `arrfree`
static char** xft_get_fonts_arr(void);
static Bool xft_font_set(struct DrawCtx* dc, char const* font_name, XftFont** fnt_out);
static char const* xft_font_name(XftFont* fnt);
static struct IOCtx ioctx_new(char const* input);
static struct IOCtx ioctx_copy(struct IOCtx const* ioctx);
static void ioctx_set(struct IOCtx* ioctx, char const* input);
static char const* ioctx_as_str(struct IOCtx const* ioctx);
static void ioctx_free(struct IOCtx* ioctx);

static Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p);
static Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Pair point_apply_trans(Pair p, Transform trans);
static Pair point_apply_trans_pivot(Pair p, Transform trans, Pair pivot);
static Pair dpt_to_pt(DPt p);
static DPt dpt_rotate(DPt p, double deg); // clockwise
static DPt dpt_add(DPt a, DPt b);

static enum ImageType file_type(u8 const* data, u32 len);
static u8* ximage_to_rgb(XImage const* image, Bool rgba);
static Bool ximage_is_valid_point(XImage const* im, i32 x, i32 y);
// coefficient c is proportional to the significance of component a
static argb argb_blend(argb a, argb b, u8 c);
// receives premultiplied argb value
static argb argb_normalize(argb c);
static argb argb_from_hsl(double hue, double sat, double light);
// XXX hex non-const because of implementation
static Bool argb_from_hex_col(char* hex, argb* argb_out);
static XRenderColor argb_to_xrender_color(argb col);
static struct Image read_file_from_memory(struct DrawCtx const* dc, u8 const* data, u32 len, argb bg);
static struct Image read_image_io(struct DrawCtx const* dc, struct IOCtx const* ioctx, argb bg);
static Bool write_io(struct DrawCtx* dc, struct Input const* input, enum ImageType type, struct IOCtx const* ioctx);
static void image_free(struct Image* im);

static ClCPrcResult cl_cmd_process(struct Ctx* ctx, struct ClCommand const* cl_cmd);
static ClCPrsResult cl_cmd_parse(struct Ctx* ctx, char const* cl);
static ClCPrsResult cl_prs_noarg(char* arg_desc_dyn, char* context_optdyn);
static ClCPrsResult cl_prs_invarg(char* arg_dyn, char* error_dyn, char* context_optdyn);
static void cl_cmd_parse_res_free(ClCPrsResult* res);
static char* cl_cmd_get_str_dyn(struct InputConsoleData const* d_cl);
static char const* cl_cmd_from_enum(enum ClCTag t);
static char const* cl_cmd_descr(enum ClCTag t);
static char const* cl_set_prop_from_enum(enum ClCDSTag t);
static char const* cl_set_prop_descr(enum ClCDSTag t);
static char const* cl_save_type_from_enum(enum ClCDSv t);
static enum ImageType cl_save_type_to_image_type(enum ClCDSv t);
// returns number of completions
static usize cl_compls_new(struct InputConsoleData* cl);
static void cl_free(struct InputConsoleData* cl);
static void cl_compls_free(struct InputConsoleData* cl);
static void cl_push(struct InputConsoleData* cl, char c);
static Bool cl_pop(struct InputConsoleData* cl);

static void input_set_damage(struct Input* inp, Rect damage);
static void input_mode_set(struct Ctx* ctx, enum InputTag mode_tag);
static void input_mode_free(struct InputMode* input_mode);
static char const* input_mode_as_str(enum InputTag mode_tag);
static void input_free(struct Input* input);

static void text_mode_push(struct Ctx* ctx, char c);
static Bool text_mode_pop(struct Ctx* ctx);
static void text_mode_rerender(struct Ctx* ctx);

static void sel_circ_init_and_show(struct Ctx* ctx, Button button, i32 x, i32 y);
static void sel_circ_free_and_hide(struct SelectionCircle* sel_circ);
static i32 sel_circ_curr_item(struct SelectionCircle const* sc, i32 x, i32 y);
// selection circle item callbacks. Can be unused, if config changed
__attribute__((unused))
static void sel_circ_on_select_tool(struct Ctx* ctx, union SCI_Arg tool);
__attribute__((unused))
static void sel_circ_on_select_figure_toggle_fill(struct Ctx* ctx, __attribute__((unused)) union SCI_Arg arg);
__attribute__((unused))
static void sel_circ_on_select_figure(struct Ctx* ctx, union SCI_Arg figure);
__attribute__((unused))
static void sel_circ_on_select_col(struct Ctx* ctx, union SCI_Arg col);
__attribute__((unused))
static void sel_circ_on_select_set_linew(struct Ctx* ctx, union SCI_Arg num);

// separate functions, because they are callbacks
static Rect tool_selection_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);
static Rect tool_text_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);
static Rect tool_selection_on_drag(struct Ctx* ctx, XMotionEvent const* event);
static Rect tool_drawer_on_press(struct Ctx* ctx, XButtonPressedEvent const* event);
static Rect tool_drawer_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);
static Rect tool_drawer_on_drag(struct Ctx* ctx, XMotionEvent const* event);
static Rect tool_figure_on_press(struct Ctx* ctx, XButtonPressedEvent const* event);
static Rect tool_figure_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);
static Rect tool_figure_on_drag(struct Ctx* ctx, XMotionEvent const* event);
static Rect tool_fill_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);
static Rect tool_picker_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);

static struct HistItem history_new_item(XImage* im, Rect rect);
static Bool history_move(struct Ctx* ctx, Bool forward);
static void history_forward(struct Ctx* ctx, struct HistItem hist);
static void history_apply(struct Ctx* ctx, struct HistItem* hist);
static void history_free(struct HistItem* hist);
static void historyarr_clear(struct HistItem** hist);

static usize ximage_data_len(XImage const* im);
static XImage* ximage_apply_xtrans(XImage* im, struct DrawCtx* dc, XTransform xtrans);
static void ximage_blend(XImage* dest, XImage* overlay);
static void ximage_clear(XImage* im);
static Bool ximage_put_checked(XImage* im, i32 x, i32 y, argb col);
static Rect ximage_flood_fill(XImage* im, argb targ_col, i32 x, i32 y);
static Rect ximage_calc_damage(XImage* im);

static Rect canvas_text(struct DrawCtx* dc, XImage* im, Pair lt_c, XftFont* font, argb col, char const* text, u32 text_len);
static Rect canvas_dash_rect(XImage* im, Pair c, Pair dims, u32 w, u32 dash_w, argb col1, argb col2);
static Rect canvas_fill_rect(XImage* im, Pair c, Pair dims, argb col);
static Rect canvas_figure(struct Ctx* ctx, XImage* im, Pair p1, Pair p2);
// line from `a` to `b` is a polygon height (a is a base);
static Rect canvas_regular_poly(XImage* im, struct ToolCtx* tc, u32 n, Pair a, Pair b);
static Rect canvas_circle(XImage* im, struct ToolCtx* tc, circle_get_alpha_fn get_a, u32 d, Pair c);
static Rect canvas_line(XImage* im, struct ToolCtx* tc, enum DrawerShape shape, Pair from, Pair to, Bool draw_first_pt);
static Rect canvas_apply_drawer(XImage* im, struct ToolCtx* tc, enum DrawerShape shape, Pair c);
static Rect canvas_copy_region(XImage* dest, XImage* src, Pair from, Pair dims, Pair to);
static void canvas_fill(XImage* im, argb col);
static Bool canvas_load(struct Ctx* ctx, struct Image* image);
static void canvas_free(struct Canvas* cv);
static void canvas_change_zoom(struct DrawCtx* dc, Pair cursor, i32 delta);
static void canvas_resize(struct Ctx* ctx, u32 new_width, u32 new_height);
static void canvas_scroll(struct Canvas* cv, Pair delta);
static void overlay_clear(struct InputOverlay* ovr);
static void overlay_expand_rect(struct InputOverlay* ovr, Rect rect);
static struct InputOverlay get_transformed_overlay(struct DrawCtx* dc, struct Input const* inp);
static void overlay_free(struct InputOverlay* ovr);

static u32 statusline_height(struct DrawCtx const* dc);
// window size - interface parts (e.g. statusline)
static Pair clientarea_size(struct DrawCtx const* dc);
static Pair canvas_size(struct DrawCtx const* dc);
static void draw_arc(struct DrawCtx* dc, Pair c, Pair dims, double a1, double a2, argb col);
static void fill_arc(struct DrawCtx* dc, Pair c, Pair dims, double a1, double a2, argb col);
static u32 draw_string(struct DrawCtx* dc, char const* str, Pair c, enum Schm sc, Bool invert);
static u32 draw_int(struct DrawCtx* dc, i32 i, Pair c, enum Schm sc, Bool invert);
static int fill_rect(struct DrawCtx* dc, Pair p, Pair dim, argb col);
static int draw_line_ex(struct DrawCtx* dc, Pair from, Pair to, u32 w, int line_style, enum Schm sc, Bool invert);
static int draw_line(struct DrawCtx* dc, Pair from, Pair to, u32 w, enum Schm sc, Bool invert);
static void draw_dash_line(struct DrawCtx* dc, Pair from, Pair to, u32 w);
static void draw_dash_rect(struct DrawCtx* dc, Pair points[4]);
// FIXME merge with get_string_rect?
static u32 get_string_width(struct DrawCtx const* dc, char const* str, u32 len);
static Rect get_string_rect(struct DrawCtx const* dc, XftFont* font, char const* str, u32 len, Pair lt_c);
static void draw_selection_circle(struct DrawCtx* dc, struct SelectionCircle const* sc, i32 pointer_x, i32 pointer_y);
static void update_screen(struct Ctx* ctx, Pair cur_scr, Bool full_redraw);
static void update_statusline(struct Ctx* ctx);
static void show_message(struct Ctx* ctx, char const* msg);

static void dc_cache_init(struct Ctx* ctx);
static void dc_cache_free(struct DrawCtx* dc);
// update Pixmaps for XRender interactions
static void dc_cache_update(struct Ctx* ctx, Rect damage);

static int trigger_clipboard_paste(struct DrawCtx* dc, Atom selection_target);

static struct Ctx ctx_init(Display* dp);
static void xextinit(Display* dp);
static void setup(Display* dp, struct Ctx* ctx);
static void run(struct Ctx* ctx);
static HdlrResult button_press_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult button_release_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult destroy_notify_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult expose_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult key_press_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult mapping_notify_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult motion_notify_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult configure_notify_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult selection_request_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult selection_notify_hdlr(struct Ctx* ctx, XEvent* event);
static HdlrResult client_message_hdlr(struct Ctx* ctx, XEvent* event);
static void cleanup(struct Ctx* ctx);
// clang-format on

static Bool is_verbose_output = False;
static Atom atoms[A_Last];
static XImage* images[I_Last];

#include "config.h"

// clang-format off
static void main_die_if_no_val_for_arg(char const* cmd_name, i32 argc, char** argv, u32 pos);
static Bool main_process_args(struct Ctx* ctx, i32 argc, char** argv);
static void main_show_help_message(FILE* out);
// clang-format on

i32 main(i32 argc, char** argv) {
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        die("cannot open X display");
    }

    struct Ctx ctx = ctx_init(display);

    if (!main_process_args(&ctx, argc, argv)) {
        main_show_help_message(stderr);
        exit(1);
    }

    xextinit(display);
    setup(display, &ctx);
    run(&ctx);
    cleanup(&ctx);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
}

void main_die_if_no_val_for_arg(char const* cmd_name, i32 argc, char** argv, u32 pos) {
    if ((i32)pos + 1 == argc || argv[pos + 1][0] == '-') {
        die("supply argument for %s", cmd_name);
    }
}

Bool main_process_args(struct Ctx* ctx, i32 argc, char** argv) {
    Bool result = True;

    for (i32 i = 1; i < argc; ++i) {
        if (argv[i][0] != '-' || !strcmp(argv[i], IOCTX_STDIO_STR)) {
            ioctx_set(&ctx->inp, argv[i]);
            ioctx_set(&ctx->out, argv[i]);
        } else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            printf("xpaint " VERSION "\n");
            exit(0);
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
            is_verbose_output = True;
        } else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--input")) {
            main_die_if_no_val_for_arg("-i or --input", argc, argv, i);
            ioctx_set(&ctx->inp, argv[++i]);
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            main_die_if_no_val_for_arg("-o or --output", argc, argv, i);
            ioctx_set(&ctx->out, argv[++i]);
        } else if (!strcmp(argv[i], "-W") || !strcmp(argv[i], "--width")) {
            main_die_if_no_val_for_arg("-W or --width", argc, argv, i);
            // ctx.dc.width == ctx.dc.cv.im->width at program start
            ctx->dc.width = strtol(argv[++i], NULL, 0);
            if (!ctx->dc.width) {
                die("canvas width must be positive number");
            }
        } else if (!strcmp(argv[i], "-H") || !strcmp(argv[i], "--height")) {
            main_die_if_no_val_for_arg("-H or --height", argc, argv, i);
            // ctx.dc.height == ctx.dc.cv.im->height at program start
            ctx->dc.height = strtol(argv[++i], NULL, 0);
            if (!ctx->dc.height) {
                die("canvas height must be positive number");
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            main_show_help_message(stdout);
            exit(0);
        } else {
            (void)fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
            result = False;
        }
    }

    return result;
}

void main_show_help_message(FILE* out) {
    (void)fprintf(
        out,
        "Usage: xpaint [OPTIONS] [FILE]\n"
        "\n"
        "Options:\n"
        "  -h, --help                   Print help message\n"
        "  -V, --version                Print version\n"
        "  -v, --verbose                Use verbose output\n"
        "  -W, --width <canvas width>   Set canvas width\n"
        "  -H, --height <canvas height> Set canvas height\n"
        "  -i, --input <file path>      Set load file\n"
        "  -o, --output <file path>     Set save file\n"
    );
}

void die(char const* errstr, ...) {
    va_list ap;

    (void)fprintf(stderr, "xpaint: ");
    va_start(ap, errstr);
    (void)vfprintf(stderr, errstr, ap);
    va_end(ap);
    (void)fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

void trace(char const* fmt, ...) {
    if (is_verbose_output) {
        va_list ap;

        va_start(ap, fmt);
        (void)vfprintf(stdout, fmt, ap);
        (void)fprintf(stdout, "\n");
        va_end(ap);
    }
}

void* ecalloc(u32 n, u32 size) {
    void* p = calloc(n, size);

    if (!p) {
        die("calloc:");
    }
    return p;
}

u32 digit_count(u32 number) {
    return (u32)floor(log10(number)) + 1;
}

void arrpoputf8(char const* strarr) {
    // from https://stackoverflow.com/a/37623867
    if (!arrlen(strarr)) {
        return;
    }

    unsigned char cp = arrpop(strarr);
    while (arrlen(strarr) && (cp & 0x80U) && !(cp & 0x40U)) {
        cp = arrpop(strarr);
    }
}

usize first_dismatch(char const* restrict s1, char const* restrict s2) {
    if (!s1 || !s2) {
        return 0;
    }
    usize offset = 0;
    for (; s1[offset] && s1[offset] == s2[offset]; ++offset) {};
    return offset;
}

struct IconData get_icon_data(enum Icon icon) {
    typedef struct IconData D;
    switch (icon) {
        case I_Select: return (D) {pic_tool_select_data, RES_SZ_TOOL_SELECT};
        case I_Pencil: return (D) {pic_tool_pencil_data, RES_SZ_TOOL_PENCIL};
        case I_Fill: return (D) {pic_tool_fill_data, RES_SZ_TOOL_FILL};
        case I_Picker: return (D) {pic_tool_picker_data, RES_SZ_TOOL_PICKER};
        case I_Brush: return (D) {pic_tool_brush_data, RES_SZ_TOOL_BRUSH};
        case I_Figure: return (D) {pic_tool_figure_data, RES_SZ_TOOL_FIGURE};
        case I_Text: return (D) {pic_tool_text_data, RES_SZ_TOOL_TEXT};
        case I_FigRect: return (D) {pic_fig_rect_data, RES_SZ_FIGURE_RECTANGLE};
        case I_FigCirc: return (D) {pic_fig_circ_data, RES_SZ_FIGURE_CIRCLE};
        case I_FigTri: return (D) {pic_fig_tri_data, RES_SZ_FIGURE_TRIANGLE};
        case I_FigFillOff: return (D) {pic_fig_fill_off_data, RES_SZ_FIGURE_FILL_OFF};
        case I_FigFillOn: return (D) {pic_fig_fill_on_data, RES_SZ_FIGURE_FILL_ON};

        case I_Last:
        case I_None: return (D) {NULL, 0};
    }
    UNREACHABLE();
}

double brush_ease(double v) {
    return (v == 1.0) ? v : 1 - pow(2, -10 * v);
}

Bool state_match(u32 a, u32 b) {
// remove button masks (Button1Mask) and ignored masks
#define CLEANMASK(p_mask) \
    ((p_mask) & (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask) & ~IGNOREMOD)

    return (a == ANY_MOD || b == ANY_MOD || CLEANMASK(a) == CLEANMASK(b));

#undef CLEANMASK
}

Button get_btn(XButtonEvent const* e) {
    return (Button) {e->button, e->state};
}

Bool btn_eq(Button a, Button b) {
    return state_match(a.mask, b.mask) && (a.button == ANY_KEY || b.button == ANY_KEY || a.button == b.button);
}

Bool key_eq(Key a, Key b) {
    return state_match(a.mask, b.mask) && (a.sym == ANY_KEY || b.sym == ANY_KEY || a.sym == b.sym);
}

static Bool can_action(struct Input const* input, Key curr_key, Action act) {
    if (!key_eq(curr_key, act.key)) {
        return False;
    }
    switch (input->mode.t) {
        case InputT_Interact: return (i32)act.mode & MF_Int;
        case InputT_Color: return (i32)act.mode & MF_Color;
        case InputT_Transform: return (i32)act.mode & MF_Trans;
        case InputT_Text:
        case InputT_Console: return False;  // managed manually
    }
    UNREACHABLE();
}

char* uri_to_path(char const* uri) {
    char const prefix[] = "file://";
    if (strncmp(uri, prefix, strlen(prefix)) != 0) {
        return NULL;
    }
    uri += strlen(prefix);

    char* result = ecalloc(strlen(uri) + 1, sizeof(char));

    char* result_ptr = result;
    for (char const* p = uri; *p; ++p) {
        if (*p == '%' && *(p + 1) && *(p + 2)) {
            char const hex[3] = {*(p + 1), *(p + 2), '\0'};
            *result_ptr++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *result_ptr++ = *p;
        }
    }
    *result_ptr = '\0';

    return result;
}

Rect rect_expand(Rect a, Rect b) {
    return (Rect) {
        .l = MIN(a.l, b.l),
        .t = MIN(a.t, b.t),
        .r = MAX(a.r, b.r),
        .b = MAX(a.b, b.b),
    };
}

Pair rect_dims(Rect a) {
    return (Pair) {a.r - a.l + 1, a.b - a.t + 1};  // inclusive
}

Transform trans_add(Transform a, Transform b) {
    return ((Transform) {.scale.x = a.scale.x * b.scale.x,
                         .scale.y = a.scale.y * b.scale.y,
                         .move.x = a.move.x + b.move.x,
                         .move.y = a.move.y + b.move.y,
                         .rotate = a.rotate + b.rotate});
}

XTransform xtrans_overlay_transform_mode(struct Input const* input) {
    if (input->mode.t != InputT_Transform) {
        return xtrans_from_trans(TRANSFORM_DEFAULT);
    }

    // actual overlay corner always at (0, 0)
    return xtrans_mult(
        xtrans_mult(
            xtrans_move(input->ovr.rect.l, input->ovr.rect.t),
            xtrans_from_trans(OVERLAY_TRANSFORM(&input->mode))
        ),
        xtrans_move(-input->ovr.rect.l, -input->ovr.rect.t)
    );
}

XTransform xtrans_scale(double x, double y) {
    return (XTransform
    ) {{{XDoubleToFixed(x), XDoubleToFixed(0.0), XDoubleToFixed(0.0)},
        {XDoubleToFixed(0.0), XDoubleToFixed(y), XDoubleToFixed(0.0)},
        {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)}}};
}

XTransform xtrans_move(double x, double y) {
    return (XTransform
    ) {{{XDoubleToFixed(1.0), XDoubleToFixed(0.0), XDoubleToFixed(x)},
        {XDoubleToFixed(0.0), XDoubleToFixed(1.0), XDoubleToFixed(y)},
        {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)}}};
}

XTransform xtrans_rotate(double a) {
    return (XTransform
    ) {{{XDoubleToFixed(cos(a)), XDoubleToFixed(-sin(a)), XDoubleToFixed(0.0)},
        {XDoubleToFixed(sin(a)), XDoubleToFixed(cos(a)), XDoubleToFixed(0.0)},
        {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)}}};
}

XTransform xtrans_from_trans(Transform trans) {
    return xtrans_mult(
        xtrans_mult(
            xtrans_move(trans.move.x, trans.move.y),  //
            xtrans_scale(trans.scale.x, trans.scale.y)
        ),
        xtrans_rotate(trans.rotate)
    );
}

XTransform xtrans_mult(XTransform a, XTransform b) {
    XTransform result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.matrix[i][j] = XDoubleToFixed(0);
            for (int k = 0; k < 3; k++) {
                result.matrix[i][j] += XDoubleToFixed(XFixedToDouble(a.matrix[i][k]) * XFixedToDouble(b.matrix[k][j]));
            }
        }
    }
    return result;
}

XTransform xtrans_invert(XTransform a) {
    double m[3][3] = {0};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i][j] = XFixedToDouble(a.matrix[i][j]);
        }
    }

    double const inv_det = 1.0 / (m[0][0] * m[1][1] - m[0][1] * m[1][0]);

    XFixed const r02 = XDoubleToFixed((m[0][1] * m[1][2] - m[1][1] * m[0][2]) * inv_det);
    XFixed const r12 = XDoubleToFixed((m[1][0] * m[0][2] - m[0][0] * m[1][2]) * inv_det);
    return (XTransform
    ) {{{XDoubleToFixed(m[1][1] * inv_det), XDoubleToFixed(-m[0][1] * inv_det), r02},
        {XDoubleToFixed(-m[1][0] * inv_det), XDoubleToFixed(m[0][0] * inv_det), r12},
        {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)}}};
}

void xwindow_set_cardinal(Display* dp, Window window, Atom key, u32 value) {
    XChangeProperty(dp, window, key, atoms[A_Cardinal], 32, PropModeReplace, (unsigned char*)&value, 1);
}

char* str_new(char const* fmt, ...) {
    va_list ap1;
    va_start(ap1, fmt);
    char* result = str_new_va(fmt, ap1);
    va_end(ap1);
    return result;
}

char* str_new_va(char const* fmt, va_list args) {
    va_list ap2;
    va_copy(ap2, args);
    usize len = vsnprintf(NULL, 0, fmt, args);
    char* result = ecalloc(len + 1, sizeof(char));
    (void)vsnprintf(result, len + 1, fmt, ap2);
    va_end(ap2);
    return result;
}

void str_free(char** str_dyn) {
    if (*str_dyn) {
        free(*str_dyn);
        *str_dyn = NULL;
    }
}

struct ToolCtx tc_new(struct DrawCtx* dc) {
    struct ToolCtx result = {
        .colarr = NULL,
        .curr_col = 0,
        .prev_col = 0,
        .line_w = TOOLS_DEFAULT_LINE_W,
    };
    arrpush(result.colarr, 0xFF000000);
    arrpush(result.colarr, 0xFFFFFFFF);
    if (!xft_font_set(dc, TEXT_TOOL_DEFAULT_FONT, &result.text_font)) {
        die("xpaint: tc_new: failed to load font: ", TEXT_TOOL_DEFAULT_FONT);
    }

    return result;
}

void tc_set_curr_col_num(struct ToolCtx* tc, u32 value) {
    tc->prev_col = tc->curr_col;
    tc->curr_col = value;
}

argb* tc_curr_col(struct ToolCtx* tc) {
    return &tc->colarr[tc->curr_col];
}

void tc_set_tool(struct ToolCtx* tc, enum ToolTag type) {
    tc->t = type;
    tc->on_press = NULL;
    tc->on_release = NULL;
    tc->on_drag = NULL;
    tc->on_move = NULL;

    switch (type) {
        case Tool_Text:
            tc->on_release = &tool_text_on_release;
            tc->d.text = (struct ToolTextData) {0};
            break;
        case Tool_Selection:
            tc->on_release = &tool_selection_on_release;
            tc->on_drag = &tool_selection_on_drag;
            break;
        case Tool_Brush:
            tc->on_press = &tool_drawer_on_press;
            tc->on_release = &tool_drawer_on_release;
            tc->on_drag = &tool_drawer_on_drag;
            tc->d.drawer = (struct DrawerData) {
                .shape = DS_Circle,
                .spacing = TOOLS_BRUSH_DEFAULT_SPACING,
            };
            break;
        case Tool_Pencil:
            tc->on_press = &tool_drawer_on_press;
            tc->on_release = &tool_drawer_on_release;
            tc->on_drag = &tool_drawer_on_drag;
            tc->d.drawer = (struct DrawerData) {
                .shape = DS_Square,
                tc->d.drawer.spacing = 1,
            };
            break;
        case Tool_Fill: tc->on_release = &tool_fill_on_release; break;
        case Tool_Picker: tc->on_release = &tool_picker_on_release; break;
        case Tool_Figure:
            tc->on_press = &tool_figure_on_press;
            tc->on_release = &tool_figure_on_release;
            tc->on_drag = &tool_figure_on_drag;
            tc->d.fig = (struct FigureData) {0};
            break;
    }
}

char const* tc_get_tool_name(struct ToolCtx const* tc) {
    switch (tc->t) {
        case Tool_Text: return "text   ";
        case Tool_Selection: return "select ";
        case Tool_Pencil: return "pencil ";
        case Tool_Fill: return "fill   ";
        case Tool_Picker: return "picker ";
        case Tool_Brush: return "brush  ";
        case Tool_Figure:
            switch (tc->d.fig.curr) {
                case Figure_Circle: return "fig:cir";
                case Figure_Rectangle: return "fig:rct";
                case Figure_Triangle: return "fig:tri";
            }
    }
    UNREACHABLE();
}

void tc_free(Display* dp, struct ToolCtx* tc) {
    if (tc->text_font != NULL) {
        XftFontClose(dp, tc->text_font);
    }
    arrfree(tc->colarr);
}

// change strcmp type to use in `qsort`
static int compare_strings(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

char** xft_get_fonts_arr(void) {
    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        trace("xpaint: xft_get_font_arr: failed to create font pattern");
        return NULL;
    }

    // only family names
    FcObjectSet* object_set = FcObjectSetBuild(FC_FAMILY, NULL);
    if (!object_set) {
        trace("xpaint: xft_get_font_arr: failed to create object set");
        FcPatternDestroy(pattern);
        return NULL;
    }

    FcFontSet* font_set = FcFontList(NULL, pattern, object_set);
    if (!font_set) {
        trace("xpaint: xft_get_font_arr: failed to retrieve font list");
        FcObjectSetDestroy(object_set);
        FcPatternDestroy(pattern);
        return NULL;
    }

    char** families_arr = NULL;
    for (int i = 0; i < font_set->nfont; i++) {
        FcPattern* font = font_set->fonts[i];
        FcChar8* family = NULL;
        // Get the first family name for this font
        if (FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch) {
            arrpush(families_arr, strdup((char*)family));
        }
    }
    if (!families_arr) {
        FcFontSetDestroy(font_set);
        FcObjectSetDestroy(object_set);
        FcPatternDestroy(pattern);
        return NULL;  // no need to filter empty array
    }

    usize const count = arrlen(families_arr);
    qsort(families_arr, count, sizeof(char*), compare_strings);

    // filter unique families
    char** result_arr = NULL;
    if (count > 0) {
        for (usize i = 0; i < count - 1; i++) {
            if (strcmp(families_arr[i], families_arr[i + 1]) != 0) {
                arrpush(result_arr, families_arr[i]);
            } else {
                free(families_arr[i]);
            }
        }
        arrpush(result_arr, families_arr[count - 1]);
    }
    // values free'd in filter loop or moved to `result`
    arrfree(families_arr);

    FcFontSetDestroy(font_set);
    FcObjectSetDestroy(object_set);
    FcPatternDestroy(pattern);

    return result_arr;
}

Bool xft_font_set(struct DrawCtx* dc, char const* font_name, XftFont** fnt_out) {
    XftFont* xfont = XftFontOpenName(dc->dp, DefaultScreen(dc->dp), font_name);
    if (!xfont) {
        // XXX never goes there (picks default font or something)
        return False;
    }
    if (*fnt_out != NULL) {
        XftFontClose(dc->dp, *fnt_out);
    }
    *fnt_out = xfont;
    return True;
}

char const* xft_font_name(XftFont* fnt) {
    char* result = NULL;
    XftPatternGetString(fnt->pattern, FC_FULLNAME, 0, &result);
    return result;
}

struct IOCtx ioctx_new(char const* input) {
    char const* uri_file_prefix = "file://";

    if (!strcmp(input, IOCTX_STDIO_STR)) {
        return (struct IOCtx) {.t = IO_Stdio};
    }
    if (!strncmp(input, uri_file_prefix, strlen(uri_file_prefix))) {
        return (struct IOCtx) {
            .t = IO_File,
            .d.file.path_dyn = uri_to_path(input),
        };
    }
    return (struct IOCtx) {
        .t = IO_File,
        .d.file.path_dyn = str_new("%s", input),
    };
}

struct IOCtx ioctx_copy(struct IOCtx const* ioctx) {
    switch (ioctx->t) {
        case IO_File: return (struct IOCtx) {.t = ioctx->t, .d.file.path_dyn = str_new("%s", ioctx->d.file.path_dyn)};
        // no dynamic data
        case IO_None:
        case IO_Stdio: return *ioctx;
    }
    UNREACHABLE();
}

void ioctx_set(struct IOCtx* ioctx, char const* input) {
    ioctx_free(ioctx);
    *ioctx = ioctx_new(input);
}

char const* ioctx_as_str(struct IOCtx const* ioctx) {
    switch (ioctx->t) {
        case IO_None: return "none";
        case IO_Stdio: return "stdio";
        case IO_File: return ioctx->d.file.path_dyn;
    }
    return "UNKNOWN";
}

void ioctx_free(struct IOCtx* ioctx) {
    switch (ioctx->t) {
        case IO_None: break;
        case IO_File: str_free(&ioctx->d.file.path_dyn); break;
        case IO_Stdio: break;
    }
    *ioctx = (struct IOCtx) {0};
}

Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p) {
    return point_from_cv_to_scr_xy(dc, p.x, p.y);
}

Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = (i32)((x * ZOOM_C(dc)) + dc->cv.scroll.x),
        .y = (i32)((y * ZOOM_C(dc)) + dc->cv.scroll.y),
    };
}

Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = (i32)((x - dc->cv.scroll.x) / ZOOM_C(dc)),
        .y = (i32)((y - dc->cv.scroll.y) / ZOOM_C(dc)),
    };
}

Pair point_apply_trans(Pair p, Transform trans) {
    XFixed(*m)[3] = xtrans_from_trans(trans).matrix;

    // m[2] is not used
    return (Pair) {
        (i32)((XFixedToDouble(m[0][0]) * p.x) + (XFixedToDouble(m[0][1]) * p.y) + (XFixedToDouble(m[0][2]) * 1)),
        (i32)((XFixedToDouble(m[1][0]) * p.x) + (XFixedToDouble(m[1][1]) * p.y) + (XFixedToDouble(m[1][2]) * 1)),
    };
}

Pair point_apply_trans_pivot(Pair p, Transform trans, Pair pivot) {
    Transform move_to_pivot = TRANSFORM_DEFAULT;

    move_to_pivot.move = (Pair) {-pivot.x, -pivot.y};
    p = point_apply_trans(p, move_to_pivot);

    p = point_apply_trans(p, trans);

    move_to_pivot.move = pivot;
    return point_apply_trans(p, move_to_pivot);
}

Pair dpt_to_pt(DPt p) {
    return (Pair) {.x = (i32)p.x, .y = (i32)p.y};
}

DPt dpt_rotate(DPt p, double deg) {
    double rad = deg * PI / 180.0;
    return (DPt) {
        .x = (cos(rad) * p.x) - (sin(rad) * p.y),
        .y = (sin(rad) * p.x) + (cos(rad) * p.y),
    };
}

DPt dpt_add(DPt a, DPt b) {
    return (DPt) {
        .x = a.x + b.x,
        .y = a.y + b.y,
    };
}

enum ImageType file_type(u8 const* data, u32 len) {
    if (!data || !len) {
        return IMT_Unknown;
    }

    // jpeg SOI marker and another marker begin
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return IMT_Jpg;
    }
    // png header
    if (len >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 && data[4] == 0x0D
        && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        return IMT_Png;
    }
    return IMT_Unknown;
}

u8* ximage_to_rgb(XImage const* image, Bool rgba) {
    u32 w = image->width;
    u32 h = image->height;
    usize pixel_size = rgba ? 4 : 3;
    usize data_size = (size_t)w * h * pixel_size;
    u8* data = (u8*)ecalloc(1, data_size);
    if (data == NULL) {
        return NULL;
    }
    i32 ii = 0;
    for (i32 y = 0; y < (i32)h; ++y) {
        for (i32 x = 0; x < (i32)w; ++x) {
            u64 pixel = XGetPixel((XImage*)image, x, y);
            data[ii + 0] = (pixel & 0xFF0000) >> 16U;
            data[ii + 1] = (pixel & 0xFF00) >> 8U;
            data[ii + 2] = (pixel & 0xFF);
            if (rgba) {
                data[ii + 3] = (pixel & ARGB_ALPHA) >> 24U;
            }
            ii += (i32)pixel_size;
        }
    }
    return data;
}

Bool ximage_is_valid_point(XImage const* im, i32 x, i32 y) {
    return BETWEEN(x, 0, im->width - 1) && BETWEEN(y, 0, im->height - 1);
}

argb argb_blend(argb a, argb b, u8 c) {
    u32 const aa = (a >> 24) & 0xFF;
    u32 const ar = (a >> 16) & 0xFF;
    u32 const ag = (a >> 8) & 0xFF;
    u32 const ab = a & 0xFF;
    u32 const ba = (b >> 24) & 0xFF;
    u32 const br = (b >> 16) & 0xFF;
    u32 const bg = (b >> 8) & 0xFF;
    u32 const bb = b & 0xFF;

    // https://stackoverflow.com/a/12016968
    // result = foreground * alpha + background * (1.0 - alpha)
    u32 const blend = c + 1;
    u32 const inv_blend = 256 - c;
    u32 const alpha = (blend * aa + inv_blend * ba) >> 8;
    u32 const red = (blend * ar + inv_blend * br) >> 8;
    u32 const green = (blend * ag + inv_blend * bg) >> 8;
    u32 const blue = (blend * ab + inv_blend * bb) >> 8;

    return alpha << 24 | red << 16 | green << 8 | blue;
}

argb argb_normalize(argb const c) {
    double const m = 255.0 / ((c >> 24) & 0xFF);
    u32 const red = MIN(0xFF, ((c >> 16) & 0xFF) * m);
    u32 const green = MIN(0xFF, ((c >> 8) & 0xFF) * m);
    u32 const blue = MIN(0xFF, (c & 0xFF) * m);

    return ARGB_ALPHA | red << 16 | green << 8 | blue;
}

argb argb_from_hsl(double hue, double sat, double light) {
    assert(hue >= 0.0 && hue <= 1.0);
    assert(sat >= 0.0 && sat <= 1.0);
    assert(light >= 0.0 && light <= 1.0);

    double const h = hue * 6.0;
    double const c = (1.0 - fabs((2.0 * light) - 1.0)) * sat;
    double const x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    double const m = light - (c / 2.0);

    double r = 0;
    double g = 0;
    double b = 0;

    if (h < 1.0) {
        r = c;
        g = x;
    } else if (h < 2.0) {
        r = x;
        g = c;
    } else if (h < 3.0) {
        g = c;
        b = x;
    } else if (h < 4.0) {
        g = x;
        b = c;
    } else if (h < 5.0) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }

    u32 const ri = (u32)((r + m) * 255.0);
    u32 const gi = (u32)((g + m) * 255.0);
    u32 const bi = (u32)((b + m) * 255.0);
    return (0xFF << 24) | (ri << 16) | (gi << 8) | bi;
}

Bool argb_from_hex_col(char* hex, argb* argb_out) {
    if (!hex || !argb_out) {
        return False;
    }
    if (*hex == '#') {
        ++hex;  // skip # at beginning
    }
    u32 const len = strlen(hex);

    switch (len) {
        default:
            // unknown format
            return False;
        case 6: {
            char* end = hex + 6;
            *argb_out = strtoul(hex, &end, 16) | ARGB_ALPHA;
        } break;
        case 8: {
            // with alpha (rrggbbaa)
            char* end = hex + 8;
            u32 const input = strtoul(hex, &end, 16);
            // convert rgba to argb
            *argb_out = ((input >> 8) & 0xFFFFFF) | ((input & 0xFF) << 24);
        } break;
    }

    return True;
}

XRenderColor argb_to_xrender_color(argb col) {
    unsigned char a = (col >> 24) & 0xFF;
    unsigned char r = (col >> 16) & 0xFF;
    unsigned char g = (col >> 8) & 0xFF;
    unsigned char b = (col) & 0xFF;

    return (XRenderColor) {
        .red = r * (0xFFFF / 0xFF),
        .green = g * (0xFFFF / 0xFF),
        .blue = b * (0xFFFF / 0xFF),
        .alpha = a * (0xFFFF / 0xFF),
    };
}

static u32 argb_to_abgr(argb v) {
    u32 const a = v & ARGB_ALPHA;
    u8 const red = (v & 0x00FF0000) >> (2 * 8);
    u32 const g = v & 0x0000FF00;
    u8 const blue = v & 0x000000FF;
    return a | blue << (2 * 8) | g | red;
}

static struct Image read_file_from_memory(struct DrawCtx const* dc, u8 const* data, u32 len, argb bg) {
    i32 width = NIL;
    i32 height = NIL;
    i32 comp = NIL;
    stbi_uc* image_data = stbi_load_from_memory(data, (i32)len, &width, &height, &comp, 4);
    if (image_data == NULL) {
        return (struct Image) {0};
    }
    // process image data
    argb* image = (argb*)image_data;
    for (i32 i = 0; i < (width * height); ++i) {
        if (bg) {
            image[i] = argb_blend(image[i], bg, (image[i] >> 24) & 0xFF);
        }
        // https://stackoverflow.com/a/17030897
        image[i] = argb_to_abgr(image[i]);
    }
    XImage* im = XCreateImage(
        dc->dp,
        dc->sys.vinfo.visual,
        dc->sys.vinfo.depth,
        ZPixmap,
        0,
        (char*)image_data,
        width,
        height,
        32,  // FIXME what is it? (must be 32)
        width * 4
    );

    return (struct Image) {.im = im, .type = file_type(data, len)};
}

struct Image read_image_io(struct DrawCtx const* dc, struct IOCtx const* ioctx, argb bg) {
    switch (ioctx->t) {
        case IO_None: return (struct Image) {0};
        case IO_File: {
            int fd = open(ioctx->d.file.path_dyn, O_RDONLY | O_CLOEXEC, 0644);
            if (fd == -1) {
                return (struct Image) {0};
            }
            off_t len = lseek(fd, 0, SEEK_END);
            void* data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
            if (!data) {
                return (struct Image) {0};
            }
            struct Image result = read_file_from_memory(dc, data, len, bg);

            munmap(data, len);
            close(fd);
            return result;
        }
        case IO_Stdio: {
            u8* data = NULL;
            u32 len = 0;
            i32 c = 0;
            // FIXME read not by 1 char?
            while ((c = getc(stdin)) != EOF) {
                len += 1;
                u8* expanded = (u8*)realloc(data, len);
                if (!expanded) {
                    die("out of memory");
                }
                data = expanded;
                data[len - 1] = c;
            }
            struct Image result = read_file_from_memory(dc, data, len, bg);

            free(data);
            return result;
        }
    }
    return (struct Image) {0};
}

struct IOCtxWriteCtx {
    struct IOCtx const* ioctx;
    Bool result_out;
};

static void ioctx_write_part(void* pctx, void* data, i32 size) {
    struct IOCtxWriteCtx* ctx = (struct IOCtxWriteCtx*)pctx;
    ctx->result_out = False;

    switch (ctx->ioctx->t) {
        case IO_None: break;
        case IO_File: {
            FILE* fd = fopen(ctx->ioctx->d.file.path_dyn, "ae");
            if (!fd) {
                trace("xpaint: failed to open file");
                return;
            }
            if (!fwrite(data, sizeof(char), size, fd)) {
                trace("xpaint: failed to write to file");
            }
            if (fclose(fd) == EOF) {
                trace("xpaint: failed to close file");
                return;
            }
        } break;
        case IO_Stdio: {
            if (!fwrite(data, sizeof(char), size, stdout)) {
                trace("xpaint: failed to write to stdout");
                return;
            }
        } break;
    }

    ctx->result_out = True;
}

Bool write_io(struct DrawCtx* dc, struct Input const* input, enum ImageType type, struct IOCtx const* ioctx) {
    if (type == IMT_Unknown) {
        return False;
    }
    Bool result = False;

    i32 w = dc->cv.im->width;
    i32 h = dc->cv.im->height;
    u8* rgba_dyn = ximage_to_rgb(dc->cv.im, True);
    if (!rgba_dyn) {
        return False;
    }

    // FIXME ask before delete/override?
    if (ioctx->t == IO_File) {
        // no check for file existence
        (void)remove(ioctx->d.file.path_dyn);
    }

    switch (type) {
        case IMT_Png: {
            stbi_write_png_compression_level = input->png_compression_level;
            struct IOCtxWriteCtx ioctx_write_ctx = {.ioctx = ioctx};
            result = stbi_write_png_to_func(&ioctx_write_part, (void*)&ioctx_write_ctx, w, h, 4, rgba_dyn, 0);
            result &= ioctx_write_ctx.result_out;
        } break;
        case IMT_Jpg: {
            i32 quality = input->jpg_quality_level;
            struct IOCtxWriteCtx ioctx_write_ctx = {.ioctx = ioctx};
            result = stbi_write_jpg_to_func(&ioctx_write_part, (void*)&ioctx_write_ctx, w, h, 4, rgba_dyn, quality);
            result &= ioctx_write_ctx.result_out;
        } break;
        case IMT_Unknown: UNREACHABLE();
    }
    free(rgba_dyn);
    return result;
}

void image_free(struct Image* im) {
    if (im->im) {
        XDestroyImage(im->im);
    }
    *im = (struct Image) {0};
}

ClCPrcResult cl_cmd_process(struct Ctx* ctx, struct ClCommand const* cl_cmd) {
    assert(cl_cmd);
    char* msg_to_show = NULL;  // counts as PCCR_Msg at func end
    usize bit_status = 0;

    switch (cl_cmd->t) {
        case ClC_Set: {
            switch (cl_cmd->d.set.t) {
                case ClCDS_LineW: {
                    CURR_TC(ctx).line_w = cl_cmd->d.set.d.line_w.value;
                } break;
                case ClCDS_Col: {
                    *tc_curr_col(&CURR_TC(ctx)) = cl_cmd->d.set.d.col.v;
                } break;
                case ClCDS_UiFont: {
                    char const* font = cl_cmd->d.set.d.ui_font.name_dyn;
                    if (!xft_font_set(&ctx->dc, font, &ctx->dc.fnt)) {
                        msg_to_show = str_new("invalid font name: '%s'", font);
                    }
                } break;
                case ClCDS_TextFont: {
                    char const* font = cl_cmd->d.set.d.text_font.name_dyn;
                    if (!xft_font_set(&ctx->dc, font, &CURR_TC(ctx).text_font)) {
                        msg_to_show = str_new("invalid font name: '%s'", font);
                    }
                } break;
                case ClCDS_Inp: {
                    char const* path = cl_cmd->d.set.d.inp.path_dyn;
                    ioctx_set(&ctx->inp, path);
                    msg_to_show = str_new("inp set to '%s'", ioctx_as_str(&ctx->inp));
                } break;
                case ClCDS_Out: {
                    char const* path = cl_cmd->d.set.d.out.path_dyn;
                    ioctx_set(&ctx->out, path);
                    msg_to_show = str_new("out set to '%s'", ioctx_as_str(&ctx->out));
                } break;
                case ClCDS_PngCompression: {
                    ctx->input.png_compression_level = cl_cmd->d.set.d.png_cpr.compression;
                } break;
                case ClCDS_JpgQuality: {
                    ctx->input.jpg_quality_level = cl_cmd->d.set.d.jpg_qlt.quality;
                } break;
                case ClCDS_Spacing: {
                    if (TC_IS_DRAWER(&CURR_TC(ctx))) {
                        if (cl_cmd->d.set.d.spacing.val >= 1) {
                            CURR_TC(ctx).d.drawer.spacing = cl_cmd->d.set.d.spacing.val;
                        } else {
                            msg_to_show = str_new("spacing must be >= 1");
                        }
                    } else {
                        msg_to_show = str_new("wrong tool to set spacing");
                    }
                } break;
                case ClCDS_Last: assert(!"invalid tag");
            }
        } break;
        case ClC_Echo: {
            msg_to_show = str_new("%s", cl_cmd->d.echo.msg_dyn);
        } break;
        case ClC_Exit: {
            bit_status |= ClCPrc_Exit;
        } break;
        case ClC_W:
        case ClC_WQ: {
            if (ctx->out.t != IO_None) {
                enum ImageType const type = IMT_Png;  // FIXME pass actual filetype
                if (write_io(&ctx->dc, &ctx->input, type, &ctx->out)) {
                    if (cl_cmd->t == ClC_WQ) {
                        bit_status |= ClCPrc_Exit;
                    } else {
                        msg_to_show = str_new("image saved to '%s'", ioctx_as_str(&ctx->out));
                    }
                } else {
                    msg_to_show = str_new("failed save image to '%s'", ioctx_as_str(&ctx->out));
                }
            } else {
                msg_to_show =
                    str_new("can't save: no path provided (use '%s' command to pass path)", cl_cmd_from_enum(ClC_Save));
            }
        } break;
        case ClC_Save: {
            if (!cl_cmd->d.save.path_dyn && ctx->out.t == IO_None) {
                msg_to_show = str_new("can't save: no path provided");
            } else {
                struct IOCtx ioctx =
                    cl_cmd->d.save.path_dyn ? ioctx_new(cl_cmd->d.save.path_dyn) : ioctx_copy(&ctx->out);
                msg_to_show = str_new(
                    write_io(&ctx->dc, &ctx->input, cl_save_type_to_image_type(cl_cmd->d.save.im_type), &ioctx)
                        ? "image saved to '%s'"
                        : "failed save image to '%s'",
                    ioctx_as_str(&ioctx)
                );
                ioctx_free(&ioctx);
            }
        } break;
        case ClC_Load: {
            struct IOCtx ioctx = cl_cmd->d.load.path_dyn ? ioctx_new(cl_cmd->d.load.path_dyn) : ioctx_copy(&ctx->inp);
            struct Image im = read_image_io(&ctx->dc, &ioctx, 0);

            XImage* old_cv = ctx->dc.cv.im;
            Rect old_cv_rect = (Rect) {0, 0, old_cv->width, old_cv->height};
            struct HistItem to_push = history_new_item(old_cv, old_cv_rect);

            if (canvas_load(ctx, &im)) {
                history_forward(ctx, to_push);
                msg_to_show = str_new("image_loaded from '%s'", ioctx_as_str(&ioctx));
            } else {
                image_free(&im);
                history_free(&to_push);
                msg_to_show = str_new("failed load image from '%s'", ioctx_as_str(&ioctx));
            }
            ioctx_free(&ioctx);
        } break;
        case ClC_Last: assert(!"invalid enum value");
    }
    bit_status |= msg_to_show ? ClCPrc_Msg : 0;
    return (ClCPrcResult) {.bit_status = bit_status, .msg_dyn = msg_to_show};
}

static ClCPrsResult cl_cmd_parse_helper(__attribute__((unused)) struct Ctx* ctx, char* cl) {
    char const* cmd = strtok(cl, CL_DELIM);
    if (!cmd) {
        return cl_prs_noarg(str_new("command"), NULL);
    }
    if (!strcmp(cmd, "echo")) {
        char const* user_msg = strtok(NULL, "");
        return (ClCPrsResult
        ) {.t = ClCPrs_Ok, .d.ok.t = ClC_Echo, .d.ok.d.echo.msg_dyn = str_new("%s", COALESCE(user_msg, ""))};
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_Set))) {
        char const* prop = strtok(NULL, CL_DELIM);
        if (!prop) {
            return cl_prs_noarg(str_new("prop to set"), NULL);
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_LineW))) {
            static const u32 MIN_LW = 1;
            static const u32 MAX_LW = 5000;  // extremely large values may lag

            char const* arg = strtok(NULL, "");
            if (!arg) {
                return cl_prs_noarg(str_new("line width"), NULL);
            }
            u32 const line_w = strtol(arg, NULL, 0);
            if (!BETWEEN(line_w, MIN_LW, MAX_LW)) {
                return cl_prs_invarg(str_new("%s", arg), str_new("value must be in [%d .. %d]", MIN_LW, MAX_LW), NULL);
            }
            return (ClCPrsResult) {
                .t = ClCPrs_Ok,
                .d.ok.t = ClC_Set,
                .d.ok.d.set.t = ClCDS_LineW,
                .d.ok.d.set.d.line_w.value = line_w,
            };
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Col))) {
            char* arg = strtok(NULL, CL_DELIM);
            argb value = 0;
            if (!arg) {
                return cl_prs_noarg(str_new("hex color"), str_new("%s", cl_set_prop_from_enum(ClCDS_Col)));
            }
            if (!argb_from_hex_col(arg, &value)) {
                return cl_prs_invarg(
                    str_new("%s", arg),
                    str_new("failed to parse hex color"),
                    str_new("%s", cl_set_prop_from_enum(ClCDS_Col))
                );
            }
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok, .d.ok.t = ClC_Set, .d.ok.d.set.t = ClCDS_Col, .d.ok.d.set.d.col.v = value};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_UiFont))) {
            char const* font = strtok(NULL, "");  // font with spaces
            if (!font) {
                return cl_prs_noarg(str_new("ui font"), NULL);
            }
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_UiFont,
                                   .d.ok.d.set.d.ui_font.name_dyn = font ? str_new("%s", font) : NULL};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_TextFont))) {
            char const* font = strtok(NULL, "");  // font with spaces
            if (!font) {
                return cl_prs_noarg(str_new("text tool font"), NULL);
            }
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_TextFont,
                                   .d.ok.d.set.d.text_font.name_dyn = font ? str_new("%s", font) : NULL};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Inp))) {
            char const* path = strtok(NULL, "");  // user can load NULL
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_Inp,
                                   .d.ok.d.set.d.inp.path_dyn = path ? str_new("%s", path) : NULL};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Out))) {
            char const* path = strtok(NULL, "");  // user can load NULL
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_Out,
                                   .d.ok.d.set.d.out.path_dyn = path ? str_new("%s", path) : NULL};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_PngCompression))) {
            char const* compression = strtok(NULL, "");
            if (!compression) {
                return cl_prs_noarg(str_new("compression value"), NULL);
            }
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_PngCompression,
                                   .d.ok.d.set.d.png_cpr.compression = (i32)strtol(compression, NULL, 0)};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_JpgQuality))) {
            char const* quality = strtok(NULL, "");
            if (!quality) {
                return cl_prs_noarg(str_new("image quality"), NULL);
            }
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_JpgQuality,
                                   .d.ok.d.set.d.jpg_qlt.quality = (i32)strtol(quality, NULL, 0)};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Spacing))) {
            char const* spacing = strtok(NULL, "");
            if (!spacing) {
                return cl_prs_noarg(str_new("spacing"), NULL);
            }
            return (ClCPrsResult) {.t = ClCPrs_Ok,
                                   .d.ok.t = ClC_Set,
                                   .d.ok.d.set.t = ClCDS_Spacing,
                                   .d.ok.d.set.d.spacing.val = strtoul(spacing, NULL, 0)};
        }
        return cl_prs_invarg(str_new("%s", prop), str_new("unknown prop"), str_new("%s", cl_cmd_from_enum(ClC_Set)));
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_Exit))) {
        return (ClCPrsResult) {.t = ClCPrs_Ok, .d.ok.t = ClC_Exit};
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_W))) {
        return (ClCPrsResult) {.t = ClCPrs_Ok, .d.ok.t = ClC_W};
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_WQ))) {
        return (ClCPrsResult) {.t = ClCPrs_Ok, .d.ok.t = ClC_WQ};
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_Save))) {
        char const* type_str = strtok(NULL, CL_DELIM);
        if (!type_str) {
            return cl_prs_noarg(str_new("file type"), NULL);
        }
        enum ClCDSv type = 0;
        for (; type < ClCDSv_Last; ++type) {
            if (!strcmp(type_str, cl_save_type_from_enum(type))) {
                break;
            }
        }
        if (type == ClCDSv_Last) {
            return cl_prs_invarg(str_new("%s", type_str), str_new("unknown type"), NULL);
        }
        char const* path = strtok(NULL, "");  // path with spaces
        return (ClCPrsResult) {.t = ClCPrs_Ok,
                               .d.ok.t = ClC_Save,
                               .d.ok.d.save.im_type = type,
                               .d.ok.d.save.path_dyn = path ? str_new("%s", path) : NULL};
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_Load))) {
        char const* path = strtok(NULL, "");  // path with spaces
        return (ClCPrsResult
        ) {.t = ClCPrs_Ok, .d.ok.t = ClC_Load, .d.ok.d.load.path_dyn = path ? str_new("%s", path) : NULL};
    }

    return cl_prs_invarg(str_new("%s", cmd), str_new("unknown command"), NULL);
}

ClCPrsResult cl_cmd_parse(struct Ctx* ctx, char const* cl) {
    assert(cl);
    char* cl_bufdyn = str_new("%s", cl);
    ClCPrsResult result = cl_cmd_parse_helper(ctx, cl_bufdyn);
    str_free(&cl_bufdyn);
    return result;
}

ClCPrsResult cl_prs_noarg(char* arg_desc_dyn, char* context_optdyn) {
    return (ClCPrsResult
    ) {.t = ClCPrs_ENoArg, .d.noarg.arg_desc_dyn = arg_desc_dyn, .d.noarg.context_optdyn = context_optdyn};
}

ClCPrsResult cl_prs_invarg(char* arg_dyn, char* error_dyn, char* context_optdyn) {
    return (ClCPrsResult) {.t = ClCPrs_EInvArg,
                           .d.invarg.arg_dyn = arg_dyn,
                           .d.invarg.error_dyn = error_dyn,
                           .d.invarg.context_optdyn = context_optdyn};
}

void cl_cmd_parse_res_free(ClCPrsResult* res) {
    switch (res->t) {
        case ClCPrs_Ok: {
            struct ClCommand* cl_cmd = &res->d.ok;
            switch (cl_cmd->t) {
                case ClC_Set:
                    switch (cl_cmd->d.set.t) {
                        case ClCDS_UiFont: free(cl_cmd->d.set.d.ui_font.name_dyn); break;
                        case ClCDS_TextFont: free(cl_cmd->d.set.d.text_font.name_dyn); break;
                        case ClCDS_Inp: free(cl_cmd->d.set.d.inp.path_dyn); break;
                        case ClCDS_Out: free(cl_cmd->d.set.d.out.path_dyn); break;
                        case ClCDS_LineW:
                        case ClCDS_Col:
                        case ClCDS_PngCompression:
                        case ClCDS_JpgQuality:
                        case ClCDS_Spacing:
                        case ClCDS_Last: break;  // no default branch to enable warnings
                    }
                    break;
                case ClC_Save: free(cl_cmd->d.save.path_dyn); break;
                case ClC_Load: free(cl_cmd->d.load.path_dyn); break;
                case ClC_Echo: free(cl_cmd->d.echo.msg_dyn); break;
                case ClC_W:
                case ClC_WQ:
                case ClC_Exit: break;
                case ClC_Last: assert(!"invalid enum value");  // no default branch to enable warnings
            }
        } break;
        case ClCPrs_EInvArg: {
            str_free(&res->d.invarg.arg_dyn);
            str_free(&res->d.invarg.error_dyn);
            str_free(&res->d.invarg.context_optdyn);
        } break;
        case ClCPrs_ENoArg: {
            str_free(&res->d.noarg.arg_desc_dyn);
            str_free(&res->d.noarg.context_optdyn);
        } break;
    }
}

char* cl_cmd_get_str_dyn(struct InputConsoleData const* d_cl) {
    usize const cmd_len = arrlen(d_cl->cmdarr);
    char* cmd_dyn = ecalloc(cmd_len + 1, sizeof(char));
    memcpy(cmd_dyn, d_cl->cmdarr, cmd_len);
    cmd_dyn[cmd_len] = '\0';
    return cmd_dyn;
}

char const* cl_cmd_from_enum(enum ClCTag t) {
    switch (t) {
        case ClC_Echo: return "echo";
        case ClC_Exit: return "q";
        case ClC_Load: return "load";
        case ClC_Save: return "save";
        case ClC_W: return "w";
        case ClC_WQ: return "wq";
        case ClC_Set: return "set";
        case ClC_Last: return "last";
    }
    UNREACHABLE();
}

char const* cl_cmd_descr(enum ClCTag t) {
    switch (t) {
        case ClC_Echo: return "print message";
        case ClC_Set: return "set property value";
        case ClC_Exit: return "exit program";
        case ClC_Save: return "save changes to provided file";
        case ClC_W: return "save changes";
        case ClC_WQ: return "save changes and exit program";
        case ClC_Load: return "load file to canvas";
        case ClC_Last: break;
    }
    UNREACHABLE();
}

static char const* cl_set_prop_from_enum(enum ClCDSTag t) {
    switch (t) {
        case ClCDS_Col: return "col";
        case ClCDS_Inp: return "inp";
        case ClCDS_Out: return "out";
        case ClCDS_UiFont: return "ui_font";
        case ClCDS_TextFont: return "font";
        case ClCDS_LineW: return "line_w";
        case ClCDS_PngCompression: return "png_cmpr";
        case ClCDS_JpgQuality: return "jpg_qlty";
        case ClCDS_Spacing: return "spacing";
        case ClCDS_Last: break;
    }
    UNREACHABLE();
}

char const* cl_set_prop_descr(enum ClCDSTag t) {
    switch (t) {
        case ClCDS_LineW: return "line width";
        case ClCDS_Col: return "tool color";
        case ClCDS_UiFont: return "interface font";
        case ClCDS_TextFont: return "text tool font";
        case ClCDS_Inp: return "input file";
        case ClCDS_Out: return "output file";
        case ClCDS_PngCompression: return "png file compression level";
        case ClCDS_JpgQuality: return "jpeg quality level";
        case ClCDS_Spacing: return "brush tool spacing";
        case ClCDS_Last: break;
    }
    UNREACHABLE();
}

static char const* cl_save_type_from_enum(enum ClCDSv t) {
    switch (t) {
        case ClCDSv_Png: return "png";
        case ClCDSv_Jpg: return "jpg";
        case ClCDSv_Last: return "last";
    }
    UNREACHABLE();
}

enum ImageType cl_save_type_to_image_type(enum ClCDSv t) {
    switch (t) {
        case ClCDSv_Png: return IMT_Png;
        case ClCDSv_Jpg: return IMT_Jpg;
        case ClCDSv_Last: UNREACHABLE();
    }
    UNREACHABLE();
}

static void for_each_enum(
    i32 enum_last,
    char const* (*enum_to_str)(i32),
    void (*callback)(i32 tag, char const* enum_str, void* data_in_out),
    void* data_in_out
) {
    for (i32 e = 0; e < enum_last; ++e) {
        char const* enum_str = enum_to_str(e);
        callback(e, enum_str, data_in_out);
    }
}

struct CompletionCallbackData {
    struct ComplsItem** result;
    char const* token;
    Bool add_delim;
    char const* (*get_descr_opt)(i32 tag);
};

static void completion_callback(i32 tag, char const* enum_str, void* user_data) {
    struct CompletionCallbackData* ctx = user_data;
    usize const token_len = strlen(ctx->token);
    usize const offset = first_dismatch(enum_str, ctx->token);
    if (offset == token_len && (offset <= strlen(enum_str))) {
        char const* prefix = ctx->add_delim && (token_len == 0) ? CL_DELIM : "";
        struct ComplsItem complt = {
            .val_dyn = str_new("%s%s", prefix, enum_str + offset),
            .descr_optdyn = ctx->get_descr_opt ? str_new("%s", ctx->get_descr_opt(tag)) : NULL,
        };
        arrpush(*ctx->result, complt);
    }
}

static void cl_compls_update_helper(
    struct ComplsItem** result_out,
    char const* token,
    char const* (*enum_to_str)(i32),
    char const* (*get_descr)(i32),
    i32 enum_last,
    Bool add_delim
) {
    if (!token || !enum_to_str || !result_out) {
        return;
    }

    struct CompletionCallbackData ctx = {result_out, token, add_delim, get_descr};
    for_each_enum(enum_last, enum_to_str, completion_callback, &ctx);
}

struct TokenCheckCallbackData {
    char const* token;
    Bool found;
};

static void check_token_callback(__attribute__((unused)) i32 tag, char const* enum_str, void* user_data) {
    struct TokenCheckCallbackData* ctx = user_data;
    if (!strcmp(ctx->token, enum_str)) {
        ctx->found = True;
    }
}

static Bool is_valid_token(char const* token, char const* (*enum_to_str)(i32), i32 enum_last) {
    struct TokenCheckCallbackData ctx = {token, False};
    for_each_enum(enum_last, enum_to_str, check_token_callback, &ctx);
    return ctx.found;
}

static char* get_dir_part(char const* path) {
    if (!path) {
        return NULL;
    }

    char* last_slash = strrchr(path, '/');
    if (!last_slash) {
        return str_new(".");
    }

    if (last_slash == path) {
        return str_new("/");
    }

    size_t dir_len = last_slash - path;
    char* dir = ecalloc(dir_len + 1, sizeof(char));
    strncpy(dir, path, dir_len);
    return dir;
}

static char const* get_base_part(char const* path) {
    if (!path) {
        return "";
    }

    char const* last_slash = strrchr(path, '/');
    if (!last_slash) {
        return path;
    }

    return last_slash + 1;
}

static char* expand_home(char const* path) {
    if (!path) {
        return NULL;
    }

    if (path[0] != '~') {
        return str_new("%s", path);
    }

    char* home = getenv("HOME");
    if (home == NULL) {
        trace("xpaint: $HOME environment variable not found (requested by ~)");
        return str_new("%s", path);
    }

    return str_new("%s%s", home, path + 1);
}

static void cl_compls_update_dirs(struct ComplsItem** result, char const* token, Bool only_dirs, Bool add_delim) {
    if (!token || !result) {
        return;
    }

    // Get directory to search in and base name to match against
    char* dir_part_dyn = get_dir_part(token);
    char* search_dir_dyn = expand_home(dir_part_dyn);
    free(dir_part_dyn);
    char const* base_name = get_base_part(token);

    DIR* dir = opendir(search_dir_dyn);
    if (!dir) {
        str_free(&search_dir_dyn);
        return;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir))) {
        struct stat st;
        char* full_path_dyn = str_new("%s/%s", search_dir_dyn, entry->d_name);
        if (stat(full_path_dyn, &st) == 0) {
            if (!only_dirs || S_ISDIR(st.st_mode)) {
                char const* filename = entry->d_name;
                if (filename[0] == '.' && (filename[1] == '\0' || (filename[1] == '.' && filename[2] == '\0'))) {
                    str_free(&full_path_dyn);
                    continue;  // Skip . and ..
                }

                usize const base_len = strlen(base_name);
                usize const offset = first_dismatch(filename, base_name);
                if (offset == base_len && (offset < strlen(filename))) {
                    // Only add space prefix if we're starting fresh
                    char const* prefix = (add_delim && !*token) ? CL_DELIM : "";
                    char const* suffix = S_ISDIR(st.st_mode) ? "/" : "";
                    struct ComplsItem complt = {
                        .val_dyn = str_new("%s%s%s", prefix, filename + offset, suffix),
                        .descr_optdyn = NULL,
                    };
                    arrpush(*result, complt);
                }
            }
        }
        str_free(&full_path_dyn);
    }
    closedir(dir);
    str_free(&search_dir_dyn);
}

static void cl_compls_update_fonts(struct ComplsItem** result, char const* token, Bool add_delim) {
    char** fonts = xft_get_fonts_arr();

    for (i32 i = 0; i < arrlen(fonts); ++i) {
        usize const offset = first_dismatch(token, fonts[i]);
        if (offset == strlen(token) && offset < strlen(fonts[i])) {
            // Only add space prefix if we're starting fresh
            char const* prefix = (add_delim && !*token) ? CL_DELIM : "";
            struct ComplsItem complt = {
                .val_dyn = str_new("%s%s", prefix, fonts[i] + offset),
                .descr_optdyn = NULL,
            };
            arrpush(*result, complt);
        }

        free(fonts[i]);
    }
    arrfree(fonts);
}

usize cl_compls_new(struct InputConsoleData* cl) {
    char* cl_buf_dyn = cl_cmd_get_str_dyn(cl);
    struct ComplsItem* result = NULL;

    usize const cl_buf_len = strlen(cl_buf_dyn);
    Bool const last_char_is_space = cl_buf_len != 0 && cl_buf_dyn[cl_buf_len - 1] == CL_DELIM[0];
    // prepend delim to completions if not provided
    Bool const add_delim = cl_buf_len != 0 && !last_char_is_space;

    // FIXME strtok in if-chain
    char const* tok1 = strtok(cl_buf_dyn, CL_DELIM);
    char const* tok2 = strtok(NULL, CL_DELIM);
    char const* tok3 = strtok(NULL, "");
    tok1 = COALESCE(tok1, "");  // don't do strtok in macro
    tok2 = COALESCE(tok2, "");
    tok3 = COALESCE(tok3, "");

    cl_compls_free(cl);

    typedef char const* (*itos_f)(i32);
    // subcommands with own completions
    if (!strcmp(tok1, cl_cmd_from_enum(ClC_Set))) {
        if (!strcmp(tok2, cl_set_prop_from_enum(ClCDS_TextFont))
            || !strcmp(tok2, cl_set_prop_from_enum(ClCDS_UiFont))) {
            cl_compls_update_fonts(&result, tok3, add_delim);
        } else if (!strcmp(tok3, "")) {
            cl_compls_update_helper(
                &result,
                tok2,
                (itos_f)&cl_set_prop_from_enum,
                (itos_f)&cl_set_prop_descr,
                ClCDS_Last,
                add_delim
            );
        }
    } else if (!strcmp(tok1, cl_cmd_from_enum(ClC_Save))) {
        // Check if tok2 is a valid save type
        if (is_valid_token(tok2, (itos_f)&cl_save_type_from_enum, ClCDSv_Last)) {
            // If we have a valid type, suggest directories
            cl_compls_update_dirs(&result, tok3, False, add_delim);
            cl->dont_append_delimeter_after_apply = True;
        } else {
            // If tok2 is empty or not a valid type, suggest types
            cl_compls_update_helper(&result, tok2, (itos_f)&cl_save_type_from_enum, NULL, ClCDSv_Last, add_delim);
        }
    } else if (!strcmp(tok1, cl_cmd_from_enum(ClC_Load))) {
        // Suggest directories to load
        cl_compls_update_dirs(&result, tok2, False, add_delim);
        cl->dont_append_delimeter_after_apply = True;
    } else if (strlen(tok2) == 0 && !last_char_is_space) {  // first token completion
        cl_compls_update_helper(&result, tok1, (itos_f)&cl_cmd_from_enum, (itos_f)&cl_cmd_descr, ClC_Last, add_delim);
    } else {
        free(cl_buf_dyn);
        return 0;
    }

    free(cl_buf_dyn);

    cl->compls_arr = result;

    return arrlen(cl->compls_arr);
}

void cl_free(struct InputConsoleData* cl) {
    arrfree(cl->cmdarr);
    cl_compls_free(cl);
}

void cl_compls_free(struct InputConsoleData* cl) {
    if (cl->compls_arr) {
        for (u32 i = 0; i < arrlen(cl->compls_arr); ++i) {
            str_free(&cl->compls_arr[i].val_dyn);
            str_free(&cl->compls_arr[i].descr_optdyn);
        }
        arrfree(cl->compls_arr);
        cl->compls_arr = NULL;
        cl->compls_curr = 0;
    }
}

void cl_push(struct InputConsoleData* cl, char c) {
    arrpush(cl->cmdarr, c);
    cl_compls_free(cl);
    if (CONSOLE_AUTO_COMPLETIONS) {
        cl_compls_new(cl);
    }
}

Bool cl_pop(struct InputConsoleData* cl) {
    if (!cl->cmdarr) {
        return False;
    }
    usize const size = arrlen(cl->cmdarr);
    if (size) {
        arrpoputf8(cl->cmdarr);
    }

    cl_compls_free(cl);
    if (CONSOLE_AUTO_COMPLETIONS) {
        cl_compls_new(cl);
    }

    return size != 0;
}

void input_set_damage(struct Input* inp, Rect damage) {
    if (!IS_RNIL(damage)) {
        if (!IS_RNIL(inp->redraw_track[0])) {
            inp->redraw_track[1] = inp->redraw_track[0];
        }
        inp->redraw_track[0] = damage;
    }
    inp->damage = damage;
}

void input_mode_set(struct Ctx* ctx, enum InputTag const mode_tag) {
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;
    struct ToolCtx* tc = &CURR_TC(ctx);

    switch (inp->mode.t) {
        case InputT_Transform: {
            struct InputOverlay transformed = get_transformed_overlay(dc, inp);

            if (!IS_RNIL(transformed.rect)) {
                history_forward(ctx, history_new_item(dc->cv.im, transformed.rect));
                ximage_blend(dc->cv.im, transformed.im);
            }
            overlay_free(&transformed);

            overlay_clear(&inp->ovr);
            update_screen(ctx, PNIL, False);
        } break;
        default: break;
    }

    input_mode_free(&inp->mode);
    inp->mode.t = mode_tag;

    switch (inp->mode.t) {
        case InputT_Color: inp->mode.d.col = (struct InputColorData) {.current_digit = 0}; break;
        case InputT_Console: {
            inp->mode.d.cl = (struct InputConsoleData) {0};
            if (CONSOLE_AUTO_COMPLETIONS) {
                cl_compls_new(&inp->mode.d.cl);
            }
        } break;
        case InputT_Interact: break;
        case InputT_Transform:
            inp->mode.d.trans = (struct InputTransformData) {
                .acc = TRANSFORM_DEFAULT,
                .curr = TRANSFORM_DEFAULT,
            };
            break;
        case InputT_Text:
            if (tc->t == Tool_Text) {
                inp->mode.d.text = (struct InputTextData) {.tool_data = tc->d.text};
            } else {
                inp->mode.d.text = (struct InputTextData) {0};
            }
            break;
    }
}

void input_mode_free(struct InputMode* input_mode) {
    switch (input_mode->t) {
        case InputT_Console: cl_free(&input_mode->d.cl); break;
        case InputT_Text: arrfree(input_mode->d.text.textarr); break;
        default: break;
    }
}

char const* input_mode_as_str(enum InputTag mode_tag) {
    switch (mode_tag) {
        case InputT_Interact: return "INT";
        case InputT_Color: return "COL";
        case InputT_Console: return "CMD";
        case InputT_Transform: return "TFM";
        case InputT_Text: return "TXT";
    }
    return "???";
}

void input_free(struct Input* input) {
    input_mode_free(&input->mode);
    overlay_free(&input->ovr);
}

void text_mode_push(struct Ctx* ctx, char c) {
    if (ctx->input.mode.t != InputT_Text) {
        return;
    }

    struct InputTextData* td = &ctx->input.mode.d.text;
    arrpush(td->textarr, c);

    text_mode_rerender(ctx);
}

Bool text_mode_pop(struct Ctx* ctx) {
    if (ctx->input.mode.t != InputT_Text) {
        return False;
    }

    struct InputTextData* td = &ctx->input.mode.d.text;
    if (!td->textarr) {
        return False;
    }

    usize const size = arrlen(td->textarr);
    if (size == 0) {
        return False;
    }
    arrpoputf8(td->textarr);

    text_mode_rerender(ctx);
    return True;
}

void text_mode_rerender(struct Ctx* ctx) {
    struct Input* inp = &ctx->input;
    struct ToolCtx* tc = &CURR_TC(ctx);
    if (inp->mode.t != InputT_Text) {
        return;
    }
    struct InputTextData* td = &inp->mode.d.text;

    input_set_damage(inp, RNIL);
    overlay_clear(&inp->ovr);
    Rect const damage = canvas_text(
        &ctx->dc,
        inp->ovr.im,
        td->tool_data.lb_corner,
        tc->text_font,
        *tc_curr_col(tc),
        td->textarr,
        arrlen(td->textarr)
    );
    inp->ovr.rect = damage;
    input_set_damage(inp, damage);
}

void sel_circ_on_select_tool(struct Ctx* ctx, union SCI_Arg tool) {
    tc_set_tool(&CURR_TC(ctx), tool.tool);
}

void sel_circ_on_select_figure_toggle_fill(struct Ctx* ctx, __attribute__((unused)) union SCI_Arg arg) {
    struct ToolCtx* tc = &CURR_TC(ctx);

    if (tc->t == Tool_Figure) {
        tc->d.fig.fill ^= 1;
    }
}

void sel_circ_on_select_figure(struct Ctx* ctx, union SCI_Arg figure) {
    struct ToolCtx* tc = &CURR_TC(ctx);

    if (tc->t == Tool_Figure) {
        tc->d.fig.curr = figure.figure;
    }
}

void sel_circ_on_select_col(struct Ctx* ctx, union SCI_Arg col) {
    *tc_curr_col(&CURR_TC(ctx)) = col.col;
}

void sel_circ_on_select_set_linew(struct Ctx* ctx, union SCI_Arg num) {
    CURR_TC(ctx).line_w = num.num;
}

void sel_circ_init_and_show(struct Ctx* ctx, Button button, i32 x, i32 y) {
    sel_circ_free_and_hide(&ctx->sc);

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct SelectionCircle* sc = &ctx->sc;

    sc->x = x;
    sc->y = y;

    switch (ctx->input.mode.t) {
        case InputT_Text:
        case InputT_Transform:
        case InputT_Console: break;
        case InputT_Color: {
            sc->draw_separators = False;

            if (btn_eq(button, BTN_SEL_CIRC)) {
                for (u32 hue_int = 0; hue_int < SEL_CIRC_COLOR_ITEMS; ++hue_int) {
                    double hue = (double)hue_int / SEL_CIRC_COLOR_ITEMS;
                    argb col = argb_from_hsl(hue, 1.0, 0.5);
                    struct Item item = {
                        .arg.col = col,
                        .on_select = &sel_circ_on_select_col,
                        .col_outer = col,
                        .col_inner = COL_BG(&ctx->dc, SchmNorm),
                    };
                    arrpush(sc->items_arr, item);
                }
            } else {
                argb base_col = *tc_curr_col(tc);
                for (u32 i = 0; i < SEL_CIRC_COLOR_ITEMS; ++i) {
                    double t = (double)i / SEL_CIRC_COLOR_ITEMS;
                    argb col = 0xFF000000;

                    double const span_grey = 0.15;
                    double const span_white = 0.2;
                    double const span_bright = 0.5;
                    double const span_dark = 0.75;
                    double const span_black = 1.0;

                    if (t < span_grey) {
                        t = t / span_grey;
                        u8 const grey = (u8)(t * 255.0);
                        col = 0xFF000000 | (grey << 16) | (grey << 8) | grey;
                    } else if (t < span_white) {
                        t = (t - span_grey) / (span_white - span_grey);
                        col = argb_blend(0xFFFFFFFF, base_col | 0x00FFFFFF, (u8)((1.0 - t) * 255.0));
                    } else if (t < span_bright) {
                        t = (t - span_white) / (span_bright - span_white);
                        col = argb_blend(base_col | 0x00FFFFFF, base_col, (u8)((1.0 - t) * 255.0));
                    } else if (t < span_dark) {
                        t = (t - span_bright) / (span_dark - span_bright);
                        col = argb_blend(base_col, base_col & 0xFF7F7F7F, (u8)((1.0 - t) * 255.0));
                    } else {
                        t = (t - span_dark) / (span_black - span_dark);
                        col = argb_blend(base_col & 0xFF7F7F7F, 0xFF000000, (u8)((1.0 - t) * 255.0));
                    }

                    struct Item item = {
                        .arg.col = col,
                        .on_select = &sel_circ_on_select_col,
                        .col_outer = col,
                        .col_inner = COL_BG(&ctx->dc, SchmNorm),
                    };
                    arrpush(sc->items_arr, item);
                }
            }
        } break;
        case InputT_Interact: {
            sc->draw_separators = True;

            if (btn_eq(button, BTN_SEL_CIRC)) {
                switch (tc->t) {
                    case Tool_Text:
                    case Tool_Selection:
                    case Tool_Pencil:
                    case Tool_Fill:
                    case Tool_Picker:
                    case Tool_Brush: {
                        struct Item items[] = {
                            {.arg.tool = Tool_Text, .on_select = &sel_circ_on_select_tool, .icon = I_Text},
                            {.arg.tool = Tool_Selection, .on_select = &sel_circ_on_select_tool, .icon = I_Select},
                            {.arg.tool = Tool_Pencil, .on_select = &sel_circ_on_select_tool, .icon = I_Pencil},
                            {.arg.tool = Tool_Fill, .on_select = &sel_circ_on_select_tool, .icon = I_Fill},
                            {.arg.tool = Tool_Picker, .on_select = &sel_circ_on_select_tool, .icon = I_Picker},
                            {.arg.tool = Tool_Brush, .on_select = &sel_circ_on_select_tool, .icon = I_Brush},
                            {.arg.tool = Tool_Figure, .on_select = &sel_circ_on_select_tool, .icon = I_Figure},
                        };
                        for (u32 i = 0; i < LENGTH(items); ++i) {
                            arrpush(sc->items_arr, items[i]);
                        }
                    } break;
                    case Tool_Figure: {
                        struct Item items[] = {
                            {.arg.figure = Figure_Circle, .on_select = &sel_circ_on_select_figure, .icon = I_FigCirc},
                            {.arg.figure = Figure_Rectangle, .on_select = &sel_circ_on_select_figure, .icon = I_FigRect
                            },
                            {.arg.figure = Figure_Triangle, .on_select = &sel_circ_on_select_figure, .icon = I_FigTri},
                            {.on_select = &sel_circ_on_select_figure_toggle_fill,
                             .icon = tc->d.fig.fill ? I_FigFillOff : I_FigFillOn},
                            {.arg.tool = Tool_Pencil, .on_select = &sel_circ_on_select_tool, .icon = I_Pencil},
                        };
                        for (u32 i = 0; i < LENGTH(items); ++i) {
                            arrpush(sc->items_arr, items[i]);
                        }
                    } break;
                }
            } else {
                for (u32 i = 0; i < LENGTH(ALTERNATIVE_SEL_CIRC_ITEMS); ++i) {
                    arrpush(sc->items_arr, ALTERNATIVE_SEL_CIRC_ITEMS[i]);
                }
            }
        } break;
    }
}

void sel_circ_free_and_hide(struct SelectionCircle* sel_circ) {
    if (sel_circ->items_arr) {
        arrfree(sel_circ->items_arr);
        sel_circ->items_arr = NULL;
    }
}

i32 sel_circ_curr_item(struct SelectionCircle const* sc, i32 x, i32 y) {
    if (sc == NULL || sc->items_arr == NULL) {
        return NIL;
    }

    i32 const pointer_x_rel = x - sc->x;
    i32 const pointer_y_rel = y - sc->y;
    if (pointer_x_rel == 0 && pointer_y_rel == 0) {
        return NIL;  // prevent 0.0 / 0.0 division
    }
    double const segment_rad = PI * 2 / MAX(1, arrlen(sc->items_arr));
    double const segment_deg = segment_rad / PI * 180;
    double const pointer_r = sqrt((pointer_x_rel * pointer_x_rel) + (pointer_y_rel * pointer_y_rel));

    if (pointer_r > SEL_CIRC_OUTER_R_PX || pointer_r < SEL_CIRC_INNER_R_PX) {
        return NIL;
    }
    // FIXME do it right
    double angle = -atan(pointer_y_rel * 1.0 / pointer_x_rel) / PI * 180;
    if (pointer_x_rel < 0) {
        angle += 180;
    } else if (pointer_y_rel >= 0) {
        angle += 360;
    }

    return (i32)(angle / segment_deg);
}

Rect tool_selection_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    if (!btn_eq(get_btn(event), BTN_MAIN) && !btn_eq(get_btn(event), BTN_COPY_SELECTION)) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;

    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    i32 const begin_x = CLAMP(inp->c.pos.x, 0, dc->cv.im->width);
    i32 const begin_y = CLAMP(inp->c.pos.y, 0, dc->cv.im->height);
    i32 const end_x = CLAMP(pointer.x, 0, dc->cv.im->width);
    i32 const end_y = CLAMP(pointer.y, 0, dc->cv.im->height);

    Pair p = {MIN(begin_x, end_x), MIN(begin_y, end_y)};
    Pair dims = (Pair) {MAX(begin_x, end_x) - p.x, MAX(begin_y, end_y) - p.y};

    input_set_damage(inp, RNIL);
    overlay_clear(&inp->ovr);

    Rect damage = canvas_copy_region(inp->ovr.im, dc->cv.im, p, dims, p);
    overlay_expand_rect(&inp->ovr, damage);

    if (!IS_RNIL(damage)) {
        // move on BTN_MAIN, copy on BTN_COPY_SELECTION
        if (!btn_eq(get_btn(event), BTN_COPY_SELECTION)) {
            history_forward(ctx, history_new_item(dc->cv.im, damage));

            argb bg_col = CANVAS_BACKGROUND;  // FIXME set in runtime?
            canvas_fill_rect(dc->cv.im, p, dims, bg_col);
        }

        input_mode_set(ctx, InputT_Transform);
    }

    // all actions already done above
    return RNIL;
}

Rect tool_text_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    if (!btn_eq(get_btn(event), BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    assert(tc->t == Tool_Text);

    tc->d.text.lb_corner = point_from_scr_to_cv_xy(&ctx->dc, event->x, event->y);
    input_mode_set(ctx, InputT_Text);

    return RNIL;
}

Rect tool_selection_on_drag(struct Ctx* ctx, XMotionEvent const* event) {
    if (!btn_eq(ctx->input.c.btn, BTN_MAIN) && !btn_eq(ctx->input.c.btn, BTN_COPY_SELECTION)) {
        return RNIL;
    }

    struct Input* inp = &ctx->input;
    struct DrawCtx* dc = &ctx->dc;
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    i32 const begin_x = MIN(pointer.x, inp->c.pos.x);
    i32 const begin_y = MIN(pointer.y, inp->c.pos.y);
    i32 const end_x = MAX(pointer.x, inp->c.pos.x);
    i32 const end_y = MAX(pointer.y, inp->c.pos.y);
    i32 const w = end_x - begin_x;
    i32 const h = end_y - begin_y;

    input_set_damage(inp, RNIL);
    overlay_clear(&inp->ovr);

    u32 const line_w = (u32)MAX(1, ceil(1.5 / ZOOM_C(dc)));

    return canvas_dash_rect(
        inp->ovr.im,
        (Pair) {begin_x, begin_y},
        (Pair) {w - 1, h - 1},  // inclusive
        line_w,
        line_w * 2,
        COL_BG(dc, SchmNorm),
        COL_FG(dc, SchmNorm)
    );
}

Rect tool_drawer_on_press(struct Ctx* ctx, XButtonPressedEvent const* event) {
    if (!btn_eq(get_btn(event), BTN_MAIN)) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    struct ToolCtx* tc = &CURR_TC(ctx);

    if (!state_match(event->state, ShiftMask)) {
        ctx->input.anchor = point_from_scr_to_cv_xy(dc, event->x, event->y);
        return canvas_apply_drawer(
            ctx->input.ovr.im,
            tc,
            tc->d.drawer.shape,
            point_from_scr_to_cv_xy(dc, event->x, event->y)
        );
    }

    return RNIL;
}

Rect tool_drawer_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    if (!btn_eq(get_btn(event), BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct DrawerData* drawer = &tc->d.drawer;

    Pair begin = ctx->input.anchor;
    Pair end = point_from_scr_to_cv_xy(dc, event->x, event->y);
    XImage* const im = ctx->input.ovr.im;

    ctx->input.anchor = end;

    // all points with drag motion drawn in on_drag
    if (ctx->input.c.state != CS_Drag && state_match(event->state, ShiftMask)) {
        return canvas_line(im, tc, drawer->shape, begin, end, False);
    }

    return RNIL;
}

Rect tool_drawer_on_drag(struct Ctx* ctx, XMotionEvent const* event) {
    if (!btn_eq(ctx->input.c.btn, BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct DrawerData const* drawer = &tc->d.drawer;
    XImage* const im = ctx->input.ovr.im;

    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair const anchor = ctx->input.anchor;

    Rect damage = canvas_line(im, tc, drawer->shape, anchor, pointer, False);

    if (!IS_RNIL(damage)) {
        ctx->input.anchor = pointer;
    }

    return damage;
}

Rect tool_figure_on_press(struct Ctx* ctx, XButtonPressedEvent const* event) {
    if (!btn_eq(get_btn(event), BTN_MAIN)) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;

    if (!state_match(event->state, ShiftMask)) {
        ctx->input.anchor = point_from_scr_to_cv_xy(dc, event->x, event->y);
    }

    return RNIL;
}

Rect tool_figure_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    if (!btn_eq(get_btn(event), BTN_MAIN)
        || (ctx->input.c.state != CS_Drag && !(state_match(event->state, ShiftMask)))) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair anchor = ctx->input.anchor;

    overlay_clear(&ctx->input.ovr);
    return canvas_figure(ctx, ctx->input.ovr.im, pointer, anchor);
}

Rect tool_figure_on_drag(struct Ctx* ctx, XMotionEvent const* event) {
    if (!btn_eq(ctx->input.c.btn, BTN_MAIN)) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair anchor = ctx->input.anchor;

    input_set_damage(&ctx->input, RNIL);  // also undo damage
    overlay_clear(&ctx->input.ovr);

    return canvas_figure(ctx, ctx->input.ovr.im, pointer, anchor);
}

Rect tool_fill_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    if (!btn_eq(ctx->input.c.btn, BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;

    // copy whole canvas to overlay (flood_fill must know surround pixels)
    usize data_len = ximage_data_len(dc->cv.im);
    // XXX is XImage::data public member?
    memcpy(inp->ovr.im->data, dc->cv.im->data, data_len);

    Pair const cur = point_from_scr_to_cv_xy(dc, event->x, event->y);

    return ximage_flood_fill(inp->ovr.im, *tc_curr_col(tc), cur.x, cur.y);
}

Rect tool_picker_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    XImage* im = dc->cv.im;

    if (!ximage_is_valid_point(im, pointer.x, pointer.y)) {
        return RNIL;
    }

    *tc_curr_col(tc) = XGetPixel(im, pointer.x, pointer.y);
    return RNIL;
}

struct HistItem history_new_item(XImage* im, Rect rect) {
    assert(!IS_RNIL(rect) && rect.l <= rect.r && rect.t <= rect.b);

    return (struct HistItem) {
        .pivot = (Pair) {rect.l, rect.t},
        .patch = XSubImage(
            im,
            rect.l,
            rect.t,
            // inclusive
            rect.r - rect.l + 1,
            rect.b - rect.t + 1
        ),
    };
}

Bool history_move(struct Ctx* ctx, Bool forward) {
    struct HistItem** hist_pop = forward ? &ctx->hist_nextarr : &ctx->hist_prevarr;
    struct HistItem** hist_save = forward ? &ctx->hist_prevarr : &ctx->hist_nextarr;

    if (!arrlenu(*hist_pop)) {
        return False;
    }

    struct HistItem curr = arrpop(*hist_pop);
    Rect curr_rect =
        (Rect) {curr.pivot.x, curr.pivot.y, curr.pivot.x + curr.patch->width, curr.pivot.y + curr.patch->height};

    arrpush(*hist_save, history_new_item(ctx->dc.cv.im, curr_rect));
    history_apply(ctx, &curr);
    history_free(&curr);

    return True;
}

void history_forward(struct Ctx* ctx, struct HistItem hist) {
    trace("xpaint: history forward");
    // next history invalidated after user action
    historyarr_clear(&ctx->hist_nextarr);
    arrpush(ctx->hist_prevarr, hist);
}

void history_apply(struct Ctx* ctx, struct HistItem* hist) {
    canvas_copy_region(
        ctx->dc.cv.im,
        hist->patch,
        (Pair) {0, 0},
        (Pair) {hist->patch->width, hist->patch->height},
        hist->pivot
    );
}

void history_free(struct HistItem* hist) {
    XDestroyImage(hist->patch);
}

void historyarr_clear(struct HistItem** histarr) {
    for (u32 i = 0; i < arrlenu(*histarr); ++i) {
        history_free(&(*histarr)[i]);
    }
    arrfree(*histarr);
}

usize ximage_data_len(XImage const* im) {
    // FIXME is it correct?
    return (usize)im->width * im->height * (im->depth / 8);
}

XImage* ximage_apply_xtrans(XImage* im, struct DrawCtx* dc, XTransform xtrans) {
    u32 const w = im->width;
    u32 const h = im->height;

    Pixmap src_pm = XCreatePixmap(dc->dp, dc->window, w, h, dc->sys.vinfo.depth);
    XPutImage(dc->dp, src_pm, dc->screen_gc, im, 0, 0, 0, 0, w, h);
    Pixmap dst_pm = XCreatePixmap(dc->dp, dc->window, w, h, dc->sys.vinfo.depth);

    Picture src = XRenderCreatePicture(dc->dp, src_pm, dc->sys.xrnd_pic_format, 0, NULL);
    Picture dst = XRenderCreatePicture(dc->dp, dst_pm, dc->sys.xrnd_pic_format, 0, NULL);

    // HACK xtrans_invert, because XRENDER missinterprets XTransform values
    XTransform xtrans_pict = xtrans_invert(xtrans);
    XRenderSetPictureTransform(dc->dp, src, &xtrans_pict);
    XRenderComposite(dc->dp, PictOpSrc, src, None, dst, 0, 0, 0, 0, 0, 0, w, h);

    XImage* result = XGetImage(dc->dp, dst_pm, 0, 0, w, h, AllPlanes, ZPixmap);

    XRenderFreePicture(dc->dp, src);
    XRenderFreePicture(dc->dp, dst);
    XFreePixmap(dc->dp, src_pm);
    XFreePixmap(dc->dp, dst_pm);

    return result;
}

void ximage_blend(XImage* dest, XImage* overlay) {
    for (i32 x = 0; x < overlay->width; ++x) {
        for (i32 y = 0; y < overlay->height; ++y) {
            argb ovr = XGetPixel(overlay, x, y);
            if (ovr & ARGB_ALPHA) {
                argb bg = XGetPixel(dest, x, y);
                // make first argument opaque and use its opacity as third argument.
                argb result = argb_blend(argb_normalize(ovr), bg, (ovr >> 0x18) & 0xFF);
                ximage_put_checked(dest, x, y, result);
            }
        }
    }
}

void ximage_clear(XImage* im) {
    u32 const data_size = ximage_data_len(im);
    // XXX is data a public member?
    memset(im->data, 0x0, data_size);
}

Bool ximage_put_checked(XImage* im, i32 x, i32 y, argb col) {
    if (!ximage_is_valid_point(im, x, y)) {
        return False;
    }
    XPutPixel(im, x, y, col);
    return True;
}

Rect ximage_flood_fill(XImage* im, argb targ_col, i32 x, i32 y) {
    assert(im);
    if (!ximage_is_valid_point(im, x, y)) {
        return RNIL;
    }

    static i32 const d_rows[] = {1, 0, 0, -1};
    static i32 const d_cols[] = {0, 1, -1, 0};

    argb const area_col = XGetPixel(im, x, y);
    if (area_col == targ_col) {
        return RNIL;
    }

    Rect damage = RNIL;
    Pair* queue_arr = NULL;
    Pair first = {x, y};
    arrpush(queue_arr, first);

    while (arrlen(queue_arr)) {
        Pair curr = arrpop(queue_arr);

        for (i32 dir = 0; dir < 4; ++dir) {
            Pair d_curr = {curr.x + d_rows[dir], curr.y + d_cols[dir]};

            if (!ximage_is_valid_point(im, d_curr.x, d_curr.y)) {
                continue;
            }

            if (XGetPixel(im, d_curr.x, d_curr.y) == area_col) {
                XPutPixel(im, d_curr.x, d_curr.y, targ_col);
                damage = rect_expand(damage, (Rect) {d_curr.x, d_curr.y, d_curr.x, d_curr.y});

                arrpush(queue_arr, d_curr);
            }
        }
    }

    arrfree(queue_arr);

    return damage;
}

Rect ximage_calc_damage(XImage* im) {
    Rect damage = RNIL;
    for (i32 i = 0; i < im->width; ++i) {
        for (i32 j = 0; j < im->height; ++j) {
            if (XGetPixel(im, i, j) != 0) {
                damage = rect_expand(damage, (Rect) {i, j, i, j});
            }
        }
    }
    return damage;
}

Rect canvas_text(struct DrawCtx* dc, XImage* im, Pair lt_c, XftFont* font, argb col, char const* text, u32 text_len) {
    Rect damage = RNIL;
    if (text == NULL || im == NULL || dc == NULL) {
        return damage;
    }

    XRenderColor renderColor = argb_to_xrender_color(col);
    XftColor color;
    if (!XftColorAllocValue(dc->dp, dc->sys.vinfo.visual, dc->sys.colmap, &renderColor, &color)) {
        trace("xpaint: canvas_text: failed to create xft color");
        return damage;
    }

    Rect const text_rect = get_string_rect(dc, font, text, text_len, lt_c);
    Pair const text_dims = rect_dims(text_rect);

    Pixmap pm = XCreatePixmap(dc->dp, dc->window, text_dims.x, text_dims.y, dc->sys.vinfo.depth);
    XftDraw* d = XftDrawCreate(dc->dp, pm, dc->sys.vinfo.visual, dc->sys.colmap);
    if (!d) {
        XFreePixmap(dc->dp, pm);
        trace("xpaint: canvas_text: failed to create xft draw");
        return damage;
    }

    // x, y == left-baseline
    XftDrawStringUtf8(d, &color, font, 0, font->height - font->descent, (FcChar8*)text, (i32)text_len);
    damage = rect_expand(damage, text_rect);

    // apply pixmap with text on input image
    XImage* image = XGetImage(dc->dp, pm, 0, 0, text_dims.x, text_dims.y, AllPlanes, ZPixmap);
    canvas_copy_region(im, image, (Pair) {0, 0}, text_dims, (Pair) {text_rect.l, text_rect.t});
    XDestroyImage(image);

    XftColorFree(dc->dp, dc->sys.vinfo.visual, dc->sys.colmap, &color);
    XftDrawDestroy(d);
    XFreePixmap(dc->dp, pm);

    return damage;
}

Rect canvas_dash_rect(XImage* im, Pair c, Pair dims, u32 w, u32 dash_w, argb col1, argb col2) {
    Rect damage = RNIL;

    Pair const corners[] = {c, {c.x + dims.x, c.y}, {c.x + dims.x, c.y + dims.y}, {c.x, c.y + dims.y}};

    for (u32 i = 0; i < LENGTH(corners); ++i) {
        Pair curr = corners[i];
        Pair const to = corners[(i + 1) % LENGTH(corners)];
        Pair const delta = {to.x - curr.x, to.y - curr.y};
        assert(delta.x == 0 || delta.y == 0);
        Pair const delta_sign = {delta.x == 0 ? 0 : delta.x > 0 ? 1 : -1, delta.y == 0 ? 0 : delta.y > 0 ? 1 : -1};

        u32 iterations = 0;
        while (!PAIR_EQ(curr, to) && iterations < 10000000) {
            Pair const lt = (Pair) {curr.x - ((i32)w / 2), curr.y - ((i32)w / 2)};
            argb col = iterations / dash_w % 2 ? col1 : col2;
            Rect d = canvas_fill_rect(im, lt, (Pair) {(i32)w, (i32)w}, col);
            damage = rect_expand(damage, d);

            curr.x += delta_sign.x;
            curr.y += delta_sign.y;
            iterations += 1;
        }
    }

    return damage;
}

Rect canvas_fill_rect(XImage* im, Pair c, Pair dims, argb col) {
    Rect damage = RNIL;

    Bool const nx = dims.x < 0;
    Bool const ny = dims.y < 0;
    for (i32 x = c.x + (nx ? dims.x : 0); x < c.x + (nx ? 0 : dims.x); ++x) {
        for (i32 y = c.y + (ny ? dims.y : 0); y < c.y + (ny ? 0 : dims.y); ++y) {
            if (ximage_put_checked(im, x, y, col)) {
                damage = rect_expand(damage, (Rect) {x, y, x, y});
            }
        }
    }

    return damage;
}

// FIXME w unused (required because of circle_get_alpha_fn)
static u8 canvas_brush_get_a(__attribute__((unused)) u32 w, double r, Pair p) {
    double const curr_r = sqrt(((p.x - r) * (p.x - r)) + ((p.y - r) * (p.y - r)));
    return (u32)((1.0 - brush_ease(curr_r / r)) * 0xFF);
}

static Pair get_fig_fill_pt(enum FigureType type, Pair a, Pair b, Pair im_dims) {
    switch (type) {
        case Figure_Circle:
        case Figure_Rectangle:
        case Figure_Triangle:
            return (Pair) {
                CLAMP((a.x + b.x) / 2, 0, im_dims.x - 1),
                CLAMP((a.y + b.y) / 2, 0, im_dims.y - 1),
            };
    }
    return (Pair) {-1, -1};  // will not fill anything
}

Rect canvas_figure(struct Ctx* ctx, XImage* im, Pair p1, Pair p2) {
    if (IS_PNIL(p1) || IS_PNIL(p2)) {
        return RNIL;
    }
    struct ToolCtx* tc = &CURR_TC(ctx);
    if (tc->t != Tool_Figure) {
        return RNIL;
    }
    struct FigureData const* fig = &tc->d.fig;
    argb const col = *tc_curr_col(tc);

    switch (fig->curr) {
        case Figure_Circle:
        case Figure_Rectangle:
        case Figure_Triangle: {
            u32 sides = fig->curr == Figure_Triangle ? 3 : fig->curr == Figure_Rectangle ? 4 : 250;
            Rect damage = canvas_regular_poly(im, tc, sides, p2, p1);
            if (fig->fill) {
                Pair const im_dims = {im->width, im->height};
                Pair const fill_pt = get_fig_fill_pt(fig->curr, p2, p1, im_dims);
                ximage_flood_fill(im, col, fill_pt.x, fill_pt.y);
            }
            return damage;
        }
    }

    return RNIL;
}

// FIXME runs in runtime
static DPt get_inr_circmr_cfs(u32 n) {
    // from https://math.stackexchange.com/a/3504360
    if (n % 2 == 0) {
        return (DPt) {0.5, 0.5};
    }
    double const h = 1;
    double const side = 2.0 * h * sin(PI / n) / (1 + cos(PI / n));
    double const inradius = side / 2.0 / tan(PI / n);
    double const circumradius = side / 2.0 / sin(PI / n);
    return (DPt) {inradius, circumradius};
}

Rect canvas_regular_poly(XImage* im, struct ToolCtx* tc, u32 n, Pair a, Pair b) {
    Rect damage = RNIL;
    DPt const inr_circmr = get_inr_circmr_cfs(n);
    DPt c = {
        a.x + ((b.x - a.x) * inr_circmr.x),
        a.y + ((b.y - a.y) * inr_circmr.x),
    };
    DPt curr = {
        (b.x - a.x) * inr_circmr.y,
        (b.y - a.y) * inr_circmr.y,
    };
    DPt prev = curr;
    for (u32 i = 0; i < n; ++i) {
        curr = dpt_rotate(curr, 360.0 / n);
        Rect line_damage =
            canvas_line(im, tc, DS_Square, dpt_to_pt(dpt_add(c, prev)), dpt_to_pt(dpt_add(c, curr)), True);
        damage = rect_expand(damage, line_damage);
        prev = curr;
    }

    return damage;
}

Rect canvas_line(
    XImage* im,
    struct ToolCtx* tc,
    enum DrawerShape const shape,
    Pair from,
    Pair const to,
    Bool draw_first_pt
) {
    u32 const CANVAS_LINE_MAX_STEPS = 1000000;

    Rect damage = RNIL;

    if (IS_PNIL(from) || IS_PNIL(to)) {
        return RNIL;
    }

    u32 const spacing = TC_IS_DRAWER(tc) ? tc->d.drawer.spacing : 1;

    i32 dx = abs(to.x - from.x);
    i32 sx = from.x < to.x ? 1 : -1;
    i32 dy = -abs(to.y - from.y);
    i32 sy = from.y < to.y ? 1 : -1;
    i32 error = dx + dy;
    i32 spacing_cnt = 0;

    u32 steps = 0;  // prevent infinite loops
    while (++steps < CANVAS_LINE_MAX_STEPS) {
        if (draw_first_pt && spacing_cnt == 0) {
            Rect pt_damage = canvas_apply_drawer(im, tc, shape, from);
            damage = rect_expand(damage, pt_damage);
        }
        if (PAIR_EQ(from, to)) {
            break;
        }
        i32 e2 = 2 * error;
        if (e2 >= dy) {
            if (from.x == to.x) {
                break;
            }
            error += dy;
            from.x += sx;
        }
        if (e2 <= dx) {
            if (from.y == to.y) {
                break;
            }
            error += dx;
            from.y += sy;
        }
        spacing_cnt = (spacing_cnt + 1) % (i32)spacing;
        draw_first_pt = True;
    }

    return damage;
}

Rect canvas_apply_drawer(XImage* im, struct ToolCtx* tc, enum DrawerShape shape, Pair c) {
    u32 const w = tc->line_w;
    switch (shape) {
        case DS_Circle: {
            u32 stroke_d = (u32)(w * BRUSH_LINE_W_MOD);
            return canvas_circle(im, tc, &canvas_brush_get_a, stroke_d, c);
        }
        case DS_Square: {
            Pair c2 = (Pair) {c.x - ((i32)w / 2), c.y - ((i32)w / 2)};
            Pair dims = (Pair) {(i32)w, (i32)w};
            return canvas_fill_rect(im, c2, dims, *tc_curr_col(tc));
        }
    }
    return RNIL;
}

Rect canvas_circle(XImage* im, struct ToolCtx* tc, circle_get_alpha_fn get_a, u32 d, Pair c) {
    Rect damage = RNIL;
    argb const col = *tc_curr_col(tc);
    u32 const w = tc->line_w;

    if (d == 1) {
        ximage_put_checked(im, c.x, c.y, col);
        return RNIL;
    }
    double const r = d / 2.0;
    double const r_sq = r * r;
    i32 const l = c.x - (i32)r;
    i32 const t = c.y - (i32)r;
    for (i32 dx = 0; dx < (i32)d; ++dx) {
        for (i32 dy = 0; dy < (i32)d; ++dy) {
            double const dr = ((dx - r) * (dx - r)) + ((dy - r) * (dy - r));
            i32 const x = l + dx;
            i32 const y = t + dy;
            if (!ximage_is_valid_point(im, x, y) || dr > r_sq) {
                continue;
            }
            argb const bg = XGetPixel(im, x, y);
            argb const blended = argb_blend(col, bg, get_a(w, r, (Pair) {dx, dy}));
            XPutPixel(im, x, y, blended);
            damage = rect_expand(damage, (Rect) {x, y, x, y});
        }
    }

    return damage;
}

Rect canvas_copy_region(XImage* dest, XImage* src, Pair from, Pair dims, Pair to) {
    i32 const w = src->width;
    i32 const h = src->height;
    assert(from.x >= 0 && from.y >= 0);
    assert(from.x + dims.x <= w && from.y + dims.y <= h);

    if (dims.x == 0 || dims.y == 0) {
        return RNIL;
    }

    // FIXME alloc only dims.x * dims.y
    u32* region_dyn = (u32*)ecalloc(w * h, sizeof(u32));
    for (i32 get_or_set = 1; get_or_set >= 0; --get_or_set) {
        for (i32 y = 0; y < dims.y; ++y) {
            for (i32 x = 0; x < dims.x; ++x) {
                if (get_or_set) {
                    region_dyn[(y * w) + x] = XGetPixel(src, from.x + x, from.y + y);
                } else {
                    ximage_put_checked(dest, to.x + x, to.y + y, region_dyn[(y * w) + x]);
                }
            }
        }
    }

    free(region_dyn);
    return (Rect) {
        MAX(0, to.x),
        MAX(0, to.y),
        MIN(dest->width - 1, to.x + dims.x - 1),
        MIN(dest->height - 1, to.y + dims.y - 1),
    };
}

void canvas_fill(XImage* im, argb col) {
    for (i32 i = 0; i < im->width; ++i) {
        for (i32 j = 0; j < im->height; ++j) {
            XPutPixel(im, i, j, col);
        }
    }
}

static Bool canvas_load(struct Ctx* ctx, struct Image* image) {
    if (!image->im) {
        return False;
    }
    struct DrawCtx* dc = &ctx->dc;

    overlay_free(&ctx->input.ovr);
    canvas_free(&dc->cv);
    dc->cv.im = image->im;
    dc->cv.type = image->type;
    ctx->input.ovr.im = XSubImage(dc->cv.im, 0, 0, dc->cv.im->width, dc->cv.im->height);
    overlay_clear(&ctx->input.ovr);
    return True;
}

void canvas_free(struct Canvas* cv) {
    if (cv->im) {
        XDestroyImage(cv->im);
        cv->im = NULL;
    }
}

void canvas_change_zoom(struct DrawCtx* dc, Pair cursor, i32 delta) {
    double old_zoom = ZOOM_C(dc);
    dc->cv.zoom = CLAMP(dc->cv.zoom + delta, CANVAS_MIN_ZOOM, CANVAS_MAX_ZOOM);
    // keep cursor at same position
    canvas_scroll(
        &dc->cv,
        (Pair) {
            (i32)((dc->cv.scroll.x - cursor.x) * (ZOOM_C(dc) / old_zoom - 1)),
            (i32)((dc->cv.scroll.y - cursor.y) * (ZOOM_C(dc) / old_zoom - 1)),
        }
    );
}

void canvas_resize(struct Ctx* ctx, u32 new_width, u32 new_height) {
    if (new_width <= 0 || new_height <= 0) {
        trace("resize_canvas: invalid canvas size");
        return;
    }
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;
    u32 const old_width = dc->cv.im->width;
    u32 const old_height = dc->cv.im->height;

    // resize overlay too
    XImage* old_overlay = inp->ovr.im;
    inp->ovr.im = XSubImage(inp->ovr.im, 0, 0, new_width, new_height);
    inp->ovr.rect.l = MIN(new_width, (u32)inp->ovr.rect.l);
    inp->ovr.rect.r = MIN(new_width, (u32)inp->ovr.rect.r);
    inp->ovr.rect.t = MIN(new_height, (u32)inp->ovr.rect.t);
    inp->ovr.rect.b = MIN(new_height, (u32)inp->ovr.rect.b);
    XDestroyImage(old_overlay);

    // FIXME can fill color be changed?
    XImage* new_cv_im = XSubImage(dc->cv.im, 0, 0, new_width, new_height);
    XDestroyImage(dc->cv.im);
    dc->cv.im = new_cv_im;

    // fill new area if needed
    if (old_width < new_width) {
        canvas_fill_rect(
            dc->cv.im,
            (Pair) {(i32)old_width, 0},
            (Pair) {(i32)(new_width - old_width), (i32)new_height},
            CANVAS_BACKGROUND
        );
    }
    if (old_height < new_height) {
        canvas_fill_rect(
            dc->cv.im,
            (Pair) {0, (i32)old_height},
            (Pair) {(i32)new_width, (i32)(new_height - old_height)},
            CANVAS_BACKGROUND
        );
    }
}

void canvas_scroll(struct Canvas* cv, Pair delta) {
    cv->scroll.x += delta.x;
    cv->scroll.y += delta.y;
}

void overlay_clear(struct InputOverlay* ovr) {
    ximage_clear(ovr->im);
    ovr->rect = RNIL;
}

void overlay_expand_rect(struct InputOverlay* ovr, Rect rect) {
    ovr->rect = rect_expand(ovr->rect, rect);
}

static struct InputOverlay get_transformed_overlay(struct DrawCtx* dc, struct Input const* inp) {
    XTransform const xtrans = xtrans_overlay_transform_mode(inp);
    XImage* im = ximage_apply_xtrans(inp->ovr.im, dc, xtrans);
    Rect const rect = ximage_calc_damage(im);
    return (struct InputOverlay) {.im = im, .rect = rect};
}

void overlay_free(struct InputOverlay* ovr) {
    if (ovr->im) {
        XDestroyImage(ovr->im);
        ovr->im = NULL;
    }
    ovr->rect = RNIL;
}

u32 statusline_height(struct DrawCtx const* dc) {
    return dc->fnt->ascent + STATUSLINE_PADDING_BOTTOM;
}

Pair clientarea_size(struct DrawCtx const* dc) {
    return (Pair) {
        .x = (i32)(dc->width),
        .y = (i32)(dc->height - statusline_height(dc)),
    };
}

Pair canvas_size(struct DrawCtx const* dc) {
    return (Pair) {
        .x = (i32)(dc->cv.im->width * ZOOM_C(dc)),
        .y = (i32)(dc->cv.im->height * ZOOM_C(dc)),
    };
}

void draw_arc(struct DrawCtx* dc, Pair c, Pair dims, double a1, double a2, argb col) {
    XSetForeground(dc->dp, dc->screen_gc, col);
    XDrawArc(dc->dp, dc->back_buffer, dc->screen_gc, c.x, c.y, dims.x, dims.y, (i32)(a1 * 64), (i32)(a2 * 64));
}

void fill_arc(struct DrawCtx* dc, Pair c, Pair dims, double a1, double a2, argb col) {
    XSetForeground(dc->dp, dc->screen_gc, col);
    XFillArc(dc->dp, dc->back_buffer, dc->screen_gc, c.x, c.y, dims.x, dims.y, (i32)(a1 * 64), (i32)(a2 * 64));
}

u32 draw_string(struct DrawCtx* dc, char const* str, Pair c, enum Schm sc, Bool invert) {
    XftDraw* d = XftDrawCreate(dc->dp, dc->back_buffer, dc->sys.vinfo.visual, dc->sys.colmap);
    u32 str_len = strlen(str);
    u32 const width = get_string_width(dc, str, str_len);
    XftDrawStringUtf8(
        d,
        invert ? &dc->schemes_dyn[sc].bg : &dc->schemes_dyn[sc].fg,
        dc->fnt,
        c.x,
        c.y,
        (FcChar8 const*)str,
        (i32)str_len
    );
    XftDrawDestroy(d);
    return width;
}

u32 draw_int(struct DrawCtx* dc, i32 i, Pair c, enum Schm sc, Bool invert) {
    char* msg = str_new("%d", i);
    u32 result = draw_string(dc, msg, c, sc, invert);
    str_free(&msg);
    return result;
}

// XXX always opaque
int fill_rect(struct DrawCtx* dc, Pair p, Pair dim, argb col) {
    XSetForeground(dc->dp, dc->screen_gc, col | 0xFF000000);
    return XFillRectangle(dc->dp, dc->back_buffer, dc->screen_gc, p.x, p.y, dim.x, dim.y);
}

int draw_line_ex(struct DrawCtx* dc, Pair from, Pair to, u32 w, int line_style, enum Schm sc, Bool invert) {
    GC gc = dc->screen_gc;
    XSetForeground(dc->dp, gc, invert ? COL_BG(dc, sc) : COL_FG(dc, sc));
    XSetLineAttributes(dc->dp, gc, w, line_style, CapButt, JoinMiter);
    return XDrawLine(dc->dp, dc->back_buffer, gc, from.x, from.y, to.x, to.y);
}

int draw_line(struct DrawCtx* dc, Pair from, Pair to, u32 w, enum Schm sc, Bool invert) {
    return draw_line_ex(dc, from, to, w, LineSolid, sc, invert);
}

void draw_dash_line(struct DrawCtx* dc, Pair from, Pair to, u32 w) {
    draw_line(dc, from, to, w, SchmNorm, True);
    draw_line_ex(dc, from, to, w, LineOnOffDash, SchmNorm, False);
}

void draw_dash_rect(struct DrawCtx* dc, Pair points[4]) {
    for (u32 i = 0; i < 4; ++i) {
        Pair const from = point_from_cv_to_scr(dc, points[i]);
        Pair const to = point_from_cv_to_scr(dc, points[(i + 1) % 4]);
        draw_dash_line(dc, from, to, 2);
    }
}

u32 get_string_width(struct DrawCtx const* dc, char const* str, u32 len) {
    XGlyphInfo ext;
    XftTextExtentsUtf8(dc->dp, dc->fnt, (XftChar8*)str, (i32)len, &ext);
    return ext.xOff;
}

Rect get_string_rect(struct DrawCtx const* dc, XftFont* font, char const* str, u32 len, Pair lt) {
    XGlyphInfo ext = {0};
    XftTextExtentsUtf8(dc->dp, font, (FcChar8 const*)str, (i32)len, &ext);
    // XftDrawStringUtf8 seems like to accept left-baseline point
    return (Rect) {
        .l = lt.x,
        .t = lt.y + font->descent - font->height,
        .r = lt.x + ext.xOff,
        .b = lt.y + font->descent,
    };
}

void draw_selection_circle(
    struct DrawCtx* dc,
    struct SelectionCircle const* sc,
    i32 const pointer_x,
    i32 const pointer_y
) {
    if (sc->items_arr == NULL || arrlen(sc->items_arr) == 0) {
        return;
    }

    double const segment_icon_location = 0.58;  // 0.5 for center
    i32 const outer_r = (i32)SEL_CIRC_OUTER_R_PX;
    i32 const inner_r = (i32)SEL_CIRC_INNER_R_PX;
    Pair const outer_c = {sc->x - outer_r, sc->y - outer_r};
    Pair const outer_dims = {outer_r * 2, outer_r * 2};
    Pair const inner_c = {sc->x - inner_r, sc->y - inner_r};
    Pair const inner_dims = {inner_r * 2, inner_r * 2};

    XSetLineAttributes(dc->dp, dc->screen_gc, SEL_CIRC_LINE_W, SEL_CIRC_LINE_STYLE, CapNotLast, JoinMiter);
    fill_arc(dc, outer_c, outer_dims, 0.0, 360.0, COL_BG(dc, SchmNorm));

    {
        double const segment_rad = PI * 2 / MAX(1, arrlen(sc->items_arr));
        double const segment_deg = segment_rad / PI * 180;

        // item's properties
        for (u32 item_num = 0; item_num < arrlen(sc->items_arr); ++item_num) {
            struct Item const* item = &sc->items_arr[item_num];
            XImage* icon = images[item->icon];
            Pair center = {
                (i32)(sc->x + (cos(-segment_rad * (item_num + 0.5)) * ((outer_r + inner_r) * segment_icon_location))),
                (i32)(sc->y + (sin(-segment_rad * (item_num + 0.5)) * ((outer_r + inner_r) * segment_icon_location))),
            };

            if (item->col_outer) {
                fill_arc(dc, outer_c, outer_dims, item_num * segment_deg, segment_deg + (1.0 / 64.0), item->col_outer);
            }

            if (item->col_inner) {
                fill_arc(dc, inner_c, inner_dims, item_num * segment_deg, segment_deg + (1.0 / 64.0), item->col_inner);
            }

            if (icon) {
                XPutImage(
                    dc->dp,
                    dc->back_buffer,
                    dc->screen_gc,
                    icon,
                    0,
                    0,
                    center.x - (icon->width / 2),
                    center.y - (icon->height / 2),
                    icon->width,
                    icon->height
                );
            }

            if (item->desc) {
                Rect const desc_rect = get_string_rect(dc, dc->fnt, item->desc, strlen(item->desc), (Pair) {0, 0});
                Pair const desc_dims = rect_dims(desc_rect);
                Pair text_center = {
                    center.x - (desc_dims.x / 2),
                    center.y + (desc_dims.y / 2) + (i32)SEL_CIRC_ITEM_ICON_MARGIN_PX + (icon ? icon->height / 2 : 0),
                };

                draw_string(dc, item->desc, text_center, SchmNorm, False);
            }
        }

        // selected item fill
        i32 const current_item = sel_circ_curr_item(sc, pointer_x, pointer_y);
        if (current_item != NIL) {
            fill_arc(dc, outer_c, outer_dims, current_item * segment_deg, segment_deg, COL_BG(dc, SchmFocus));
            fill_arc(dc, inner_c, inner_dims, current_item * segment_deg, segment_deg, COL_BG(dc, SchmNorm));
        }

        // separators
        if (sc->draw_separators && arrlen(sc->items_arr) >= 2) {
            XSetForeground(dc->dp, dc->screen_gc, COL_FG(dc, SchmNorm));
            for (u32 line_num = 0; line_num < arrlen(sc->items_arr); ++line_num) {
                XDrawLine(
                    dc->dp,
                    dc->back_buffer,
                    dc->screen_gc,
                    sc->x + (i32)(cos(segment_rad * line_num) * inner_r),
                    sc->y + (i32)(sin(segment_rad * line_num) * inner_r),
                    sc->x + (i32)(cos(segment_rad * line_num) * outer_r),
                    sc->y + (i32)(sin(segment_rad * line_num) * outer_r)
                );
            }
        }
    }

    /* lines */ {
        draw_arc(dc, inner_c, inner_dims, 0.0, 360.0, COL_FG(dc, SchmNorm));
        draw_arc(dc, outer_c, outer_dims, 0.0, 360.0, COL_FG(dc, SchmNorm));
    }

    XdbeSwapBuffers(
        dc->dp,
        &(XdbeSwapInfo) {
            .swap_window = dc->window,
            .swap_action = 0,
        },
        1
    );
}

void update_screen(struct Ctx* ctx, Pair cur_scr, Bool full_redraw) {
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;
    struct InputMode* mode = &inp->mode;
    struct ToolCtx* tc = &CURR_TC(ctx);

    /* draw canvas */ {
        fill_rect(dc, (Pair) {0, 0}, (Pair) {(i32)dc->width, (i32)dc->height}, WND_BACKGROUND);
        /* put scaled image */ {
            dc_cache_update(
                ctx,
                full_redraw ? (Rect) {0, 0, dc->cv.im->width, dc->cv.im->height}
                            : rect_expand(inp->redraw_track[0], inp->redraw_track[1])
            );
            //  https://stackoverflow.com/a/66896097

            Picture cv_pict = XRenderCreatePicture(
                dc->dp,
                dc->cache.pm,
                dc->sys.xrnd_pic_format,
                0,
                &(XRenderPictureAttributes) {.subwindow_mode = IncludeInferiors}
            );
            Picture overlay_pict = XRenderCreatePicture(
                dc->dp,
                dc->cache.overlay,
                dc->sys.xrnd_pic_format,
                0,
                &(XRenderPictureAttributes) {.subwindow_mode = IncludeInferiors}
            );
            Picture bb_pict = XRenderCreatePicture(
                dc->dp,
                dc->back_buffer,
                dc->sys.xrnd_pic_format,
                0,
                &(XRenderPictureAttributes) {.subwindow_mode = IncludeInferiors}
            );

            XTransform const xtrans_zoom = xtrans_scale(ZOOM_C(dc), ZOOM_C(dc));

            // HACK xtrans_invert, because XRENDER missinterprets XTransform values
            XTransform xtrans_canvas = xtrans_invert(xtrans_zoom);
            XTransform xtrans_overlay = xtrans_invert(xtrans_mult(xtrans_zoom, xtrans_overlay_transform_mode(inp)));
            XRenderSetPictureTransform(dc->dp, cv_pict, &xtrans_canvas);
            XRenderSetPictureTransform(dc->dp, overlay_pict, &xtrans_overlay);

            Pair const cv_size = canvas_size(dc);
            // clang-format off
            XRenderComposite(
                dc->dp, PictOpSrc,
                cv_pict, None,
                bb_pict,
                0, 0,
                0, 0,
                dc->cv.scroll.x, dc->cv.scroll.y,
                cv_size.x, cv_size.y
            );
            XRenderComposite(
                dc->dp, PictOpOver,
                overlay_pict, None,
                bb_pict,
                0, 0,
                0, 0,
                dc->cv.scroll.x, dc->cv.scroll.y,
                cv_size.x, cv_size.y
            );
            // clang-format on

            XRenderFreePicture(dc->dp, cv_pict);
            XRenderFreePicture(dc->dp, overlay_pict);
            XRenderFreePicture(dc->dp, bb_pict);
        }
    }
    // transform mode rectangle
    if (ctx->input.mode.t == InputT_Transform) {
        Transform const trans = OVERLAY_TRANSFORM(&ctx->input.mode);
        Rect const rect = inp->ovr.rect;
        Pair const pivot = {rect.l, rect.t};
        draw_dash_rect(
            dc,
            (Pair[4]) {point_apply_trans_pivot((Pair) {rect.l, rect.t}, trans, pivot),
                       point_apply_trans_pivot((Pair) {rect.r + 1, rect.t}, trans, pivot),
                       point_apply_trans_pivot((Pair) {rect.r + 1, rect.b + 1}, trans, pivot),
                       point_apply_trans_pivot((Pair) {rect.l, rect.b + 1}, trans, pivot)}
        );
    }

    if (mode->t == InputT_Text) {
        Rect const text_rect = get_string_rect(
            dc,
            tc->text_font,
            mode->d.text.textarr,
            arrlen(mode->d.text.textarr),
            mode->d.text.tool_data.lb_corner
        );
        draw_dash_rect(
            dc,
            // +1 to match Transform rect (idk why it was added)
            (Pair[4]) {(Pair) {text_rect.l, text_rect.t},
                       (Pair) {text_rect.r + 1, text_rect.t},
                       (Pair) {text_rect.r + 1, text_rect.b + 1},
                       (Pair) {text_rect.l, text_rect.b + 1}}
        );
    }

    if (WND_ANCHOR_CROSS_SIZE && ctx->input.mode.t == InputT_Interact && !IS_PNIL(inp->anchor)
        && inp->c.state == CS_None) {
        i32 const size = WND_ANCHOR_CROSS_SIZE;
        Pair c = point_from_cv_to_scr(dc, inp->anchor);
        Rect const rect = {c.x - size, c.y - size, c.x + size, c.y + size};
        draw_dash_line(dc, (Pair) {rect.l, rect.b}, (Pair) {rect.r, rect.t}, 1);
        draw_dash_line(dc, (Pair) {rect.l, rect.t}, (Pair) {rect.r, rect.b}, 1);
    }

    if (inp->c.state == CS_Drag && btn_eq(ctx->input.c.btn, BTN_CANVAS_RESIZE) && inp->mode.t == InputT_Interact) {
        Pair const cur = point_from_scr_to_cv_xy(dc, cur_scr.x, cur_scr.y);

        draw_dash_rect(
            dc,
            (Pair[4]) {
                (Pair) {0, 0},
                (Pair) {cur.x, 0},
                (Pair) {cur.x, cur.y},
                (Pair) {0, cur.y},
            }
        );
    }

    update_statusline(ctx);  // backbuffer swapped here
}

static u32 draw_module(struct Ctx* ctx, SLModule const* module, Pair c) {
    struct DrawCtx* dc = &ctx->dc;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct InputMode* mode = &ctx->input.mode;

    switch (module->t) {
        case SLM_Spacer: return module->d.spacer;
        case SLM_Text: {
            char const* str = module->d.text;
            return draw_string(dc, str, c, SchmNorm, False);
        } break;
        case SLM_ToolCtx: {
            u32 x = c.x;
            for (u32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                enum Schm const schm = ctx->curr_tc == (tc_name - 1) ? SchmFocus : SchmNorm;
                x += draw_int(dc, (i32)tc_name, (Pair) {(i32)x, c.y}, schm, False);
                x += STATUSLINE_MODULE_SPACING_SMALL_PX;
            }
            return x - STATUSLINE_MODULE_SPACING_SMALL_PX;
        } break;
        case SLM_Mode: {
            char const* name = input_mode_as_str(mode->t);
            enum Schm const schm = mode->t == InputT_Interact ? SchmNorm : SchmFocus;
            return draw_string(dc, name, c, schm, False);
        } break;
        case SLM_Tool: {
            char const* name = tc_get_tool_name(tc);
            return draw_string(dc, name, c, SchmNorm, False);
        } break;
        case SLM_ToolSettings: {
            switch (mode->t) {
                case InputT_Color:
                case InputT_Console:
                case InputT_Transform: return 0;
                case InputT_Interact:
                    switch (tc->t) {
                        case Tool_Selection:
                        case Tool_Fill:
                        case Tool_Picker:
                        case Tool_Figure:
                        case Tool_Text: return 0;
                        case Tool_Pencil: {
                            return draw_int(dc, (i32)tc->line_w, c, SchmNorm, False);
                        }
                        case Tool_Brush: {
                            i32 const val = TC_IS_DRAWER(tc) ? (i32)tc->d.drawer.spacing : 0;
                            return draw_int(dc, val, c, SchmNorm, False);
                        }
                    }
                    UNREACHABLE();
                case InputT_Text: {
                    char const* separator = " ";
                    char const* font_name = xft_font_name(tc->text_font);
                    char* str = ecalloc(
                        (LENGTH(TEXT_FONT_PROMPT) - 1) + (strlen(font_name) + 2) + strlen(separator)
                            + (LENGTH(TEXT_MODE_PROMPT) - 1) + arrlen(mode->d.text.textarr) + 1,
                        sizeof(char)
                    );
                    strcat(str, TEXT_FONT_PROMPT);
                    strcat(str, "\"");
                    strcat(str, font_name);
                    strcat(str, "\"");
                    strcat(str, separator);
                    strcat(str, TEXT_MODE_PROMPT);
                    strncat(str, mode->d.text.textarr, arrlen(mode->d.text.textarr));
                    u32 width = draw_string(dc, str, c, SchmNorm, False);

                    free(str);

                    return width;
                }
            }
            UNREACHABLE();
        }
        case SLM_ColorBox: {
            fill_rect(
                dc,
                (Pair) {c.x, clientarea_size(dc).y},
                (Pair) {(i32)module->d.color_box_w, (i32)statusline_height(dc)},
                *tc_curr_col(tc)
            );
            return module->d.color_box_w;
        }
        case SLM_ColorName: {
            static u32 const col_value_size = 1 + 6;

            char col_value[col_value_size + 1];
            (void)sprintf(col_value, "#%06X", *tc_curr_col(tc) & 0xFFFFFF);
            u32 width = draw_string(dc, col_value, c, SchmNorm, False);
            // draw focused digit
            if (mode->t == InputT_Color) {
                static u32 const hash_w = 1;
                u32 const curr_dig = mode->d.col.current_digit;
                char const col_digit_value[] = {[0] = col_value[curr_dig + hash_w], [1] = '\0'};
                u32 focus_offset = get_string_width(dc, col_value, curr_dig + hash_w);
                draw_string(dc, col_digit_value, (Pair) {c.x + (i32)focus_offset, c.y}, SchmFocus, False);
            }
            return width;
        }
        case SLM_ColorList: {
            // FIXME why it compiles
            char col_count[(digit_count(MAX_COLORS) * 2) + 1 + 1];
            (void)sprintf(col_count, "%d/%td", tc->curr_col + 1, arrlen(tc->colarr));
            return draw_string(dc, col_count, c, SchmNorm, False);
        }
    }

    UNREACHABLE();
}

void update_statusline(struct Ctx* ctx) {
    struct DrawCtx* dc = &ctx->dc;
    struct InputMode* mode = &ctx->input.mode;
    u32 const statusline_h = statusline_height(dc);
    Pair const clientarea = clientarea_size(dc);
    Pair const line_dims = {(i32)dc->width, (i32)(statusline_h)};

    fill_rect(dc, (Pair) {0, clientarea.y}, line_dims, COL_BG(dc, SchmNorm));

    if (mode->t == InputT_Console) {
        struct InputConsoleData* cl = &mode->d.cl;
        i32 const cmd_y = (i32)(dc->height - STATUSLINE_PADDING_BOTTOM);

        i32 user_cmd_w = NIL;
        /* draw command */ {
            char const* command = cl->cmdarr;
            usize const command_len = arrlen(command);

            char* cl_str_dyn = ecalloc(strlen(CL_CMD_PROMPT) + command_len + 1, sizeof(char));
            strcat(cl_str_dyn, CL_CMD_PROMPT);
            strncat(cl_str_dyn + strlen(CL_CMD_PROMPT), command, command_len);

            user_cmd_w = (i32)draw_string(dc, cl_str_dyn, (Pair) {0, cmd_y}, SchmNorm, False);

            str_free(&cl_str_dyn);
        }

        /* draw cursor */ {
            i32 const indent = 2;
            i32 const cursor_w = 1;

            i32 const cursor_width = (i32)get_string_width(dc, "_", 1);
            Pair const c = {user_cmd_w, cmd_y + indent};
            draw_line(dc, c, (Pair) {c.x + cursor_width, c.y}, cursor_w, SchmNorm, False);
        }

        if (cl->compls_arr) {
            draw_string(dc, cl->compls_arr[cl->compls_curr].val_dyn, (Pair) {user_cmd_w, cmd_y}, SchmFocus, False);

            /* draw compls array */ {
                u32 const list_pre_half = (STATUSLINE_COMPLS_LIST_MAX + 1) / 2;
                u32 const len = arrlen(cl->compls_arr);

                i32 const begin =
                    MAX(0, MIN((i32)cl->compls_curr - (i32)list_pre_half, (i32)len - (i32)STATUSLINE_COMPLS_LIST_MAX));
                i32 const end = MIN(len, begin + STATUSLINE_COMPLS_LIST_MAX);

                for (i32 i = begin; i < end; ++i) {
                    char const* const compl_val = cl->compls_arr[i].val_dyn;
                    char const* const compl_descr = cl->compls_arr[i].descr_optdyn;

                    u32 const row_num = end - i;
                    i32 const y = (i32)(clientarea.y - (statusline_h * row_num));
                    i32 const stry = (i32)(y + statusline_h - STATUSLINE_PADDING_BOTTOM);
                    argb const bg_col = i != (i32)cl->compls_curr ? COL_BG(dc, SchmNorm) : COL_FG(dc, SchmNorm);
                    Bool const invert = i == (i32)cl->compls_curr;

                    fill_rect(dc, (Pair) {0, y}, line_dims, bg_col);
                    draw_string(dc, compl_val, (Pair) {0, stry}, SchmNorm, invert);

                    if (compl_descr) {
                        i32 const padding = (i32)STATUSLINE_COMPLS_DESRCIPTION_PADDING_PX;
                        i32 const val_w = (i32)get_string_width(dc, compl_val, strlen(compl_val));
                        i32 const descr_w = (i32)get_string_width(dc, compl_descr, strlen(compl_descr));
                        i32 const strx =
                            val_w + descr_w + padding < clientarea.x ? clientarea.x - descr_w : val_w + padding;
                        draw_string(dc, compl_descr, (Pair) {strx, stry}, SchmNorm, invert);
                    }
                }
            }
        }
    } else {
        u32 const y = dc->height - STATUSLINE_PADDING_BOTTOM;

        {
            // current module left-bottom corner
            u32 x = 0;
            for (u32 i = 0; i < LENGTH(LEFT_MODULES); ++i) {
                SLModule const* module = &LEFT_MODULES[i];

                x += draw_module(ctx, module, (Pair) {(i32)x, (i32)y});
                x += STATUSLINE_MODULE_SPACING_PX;
            }
        }

        {
            // current module left-bottom corner
            u32 x = dc->width;
            for (i32 i = LENGTH(RIGHT_MODULES) - 1; i >= 0; --i) {
                SLModule const* module = &RIGHT_MODULES[i];
                // HACK draw out of client area to get module width
                x -= draw_module(ctx, module, (Pair) {1000000, (i32)y});
                draw_module(ctx, module, (Pair) {(i32)x, (i32)y});
                x -= STATUSLINE_MODULE_SPACING_PX;
            }
        }
    }

    XdbeSwapBuffers(
        dc->dp,
        &(XdbeSwapInfo) {
            .swap_window = dc->window,
            .swap_action = 0,
        },
        1
    );

    XSyncSetCounter(dc->dp, ctx->xsync.counter, ctx->xsync.last_request_value);
}

// FIXME DRY
void show_message(struct Ctx* ctx, char const* msg) {
    u32 const statusline_h = statusline_height(&ctx->dc);
    fill_rect(
        &ctx->dc,
        (Pair) {0, (i32)(ctx->dc.height - statusline_h)},
        (Pair) {(i32)ctx->dc.width, (i32)statusline_h},
        COL_BG(&ctx->dc, SchmNorm)
    );
    draw_string(&ctx->dc, msg, (Pair) {0, (i32)(ctx->dc.height - STATUSLINE_PADDING_BOTTOM)}, SchmNorm, False);
    XdbeSwapBuffers(
        ctx->dc.dp,
        &(XdbeSwapInfo) {
            .swap_window = ctx->dc.window,
            .swap_action = 0,
        },
        1
    );
}

static void dc_cache_update_pm(struct DrawCtx* dc, Pixmap pm, XImage* im, Rect damage) {
    if (IS_RNIL(damage)) {
        return;
    }
    assert(im);
    Pair dims = rect_dims(damage);
    XPutImage(dc->dp, pm, dc->screen_gc, im, damage.l, damage.t, damage.l, damage.t, dims.x, dims.y);
}

void dc_cache_update(struct Ctx* ctx, Rect damage) {
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;
    Rect const cv_rect = {0, 0, dc->cv.im->width, dc->cv.im->height};
    assert(dc->cache.overlay && dc->cache.pm);

    // resize pixmaps if needed
    if (dc->cache.dims.x != dc->cv.im->width || dc->cache.dims.y != dc->cv.im->height) {
        dc_cache_free(dc);
        dc_cache_init(ctx);
        dc_cache_update_pm(dc, dc->cache.pm, dc->cv.im, cv_rect);
        dc_cache_update_pm(dc, dc->cache.overlay, inp->ovr.im, cv_rect);
    } else {
        dc_cache_update_pm(dc, dc->cache.pm, dc->cv.im, damage);
        dc_cache_update_pm(dc, dc->cache.overlay, inp->ovr.im, damage);
    }
}

int trigger_clipboard_paste(struct DrawCtx* dc, Atom selection_target) {
    return XConvertSelection(dc->dp, atoms[A_Clipboard], selection_target, atoms[A_XSelData], dc->window, CurrentTime);
}

void dc_cache_init(struct Ctx* ctx) {
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;
    assert(dc->cache.pm == 0 && dc->cache.overlay == 0);
    assert(dc->cv.im->width == inp->ovr.im->width);
    assert(dc->cv.im->height == inp->ovr.im->height);

    dc->cache.pm = XCreatePixmap(dc->dp, dc->window, dc->cv.im->width, dc->cv.im->height, dc->sys.vinfo.depth);

    dc->cache.overlay = XCreatePixmap(dc->dp, dc->window, inp->ovr.im->width, inp->ovr.im->height, dc->sys.vinfo.depth);

    dc->cache.dims.x = dc->cv.im->width;
    dc->cache.dims.y = dc->cv.im->height;
}

void dc_cache_free(struct DrawCtx* dc) {
    if (dc->cache.pm != 0) {
        XFreePixmap(dc->dp, dc->cache.pm);
        dc->cache.pm = 0;
    }
    if (dc->cache.overlay != 0) {
        XFreePixmap(dc->dp, dc->cache.overlay);
        dc->cache.overlay = 0;
    }
}

struct Ctx ctx_init(Display* dp) {
    return (struct Ctx) {
        .dc =
            (struct DrawCtx) {
                .dp = dp,
                .width = CANVAS_DEFAULT_WIDTH,
                .height = CANVAS_DEFAULT_HEIGHT,
                .cv =
                    (struct Canvas) {
                        .im = NULL,
                        .type = IMT_Png,  // save as png by default
                        .zoom = 0,
                        .scroll = {0, 0},
                    },
                .cache = (struct Cache) {.pm = 0},
            },
        .input =
            (struct Input) {
                .mode.t = InputT_Interact,
                .anchor = PNIL,
                .png_compression_level = PNG_DEFAULT_COMPRESSION,
                .jpg_quality_level = JPG_DEFAULT_QUALITY,
            },
        .inp = (struct IOCtx) {.t = IO_None},
        .out = (struct IOCtx) {.t = IO_None},
        .sel_buf.im = NULL,
        .tcarr = NULL,
        .curr_tc = 0,
        .hist_nextarr = NULL,
        .hist_prevarr = NULL,
        .sc.items_arr = NULL,
    };
}

void xextinit(Display* dp) {
    i32 maj = NIL;
    i32 min = NIL;
    if (!XdbeQueryExtension(dp, &maj, &min)) {
        die("no X Double Buffer Extention support");
    }
    if (!XSyncInitialize(dp, &maj, &min)) {
        // not critical if xserver doesn't support this
        (void)fprintf(stderr, "no SYNC Extention support\n");
    }
}

void setup(Display* dp, struct Ctx* ctx) {
    assert(dp);
    assert(ctx);

    /* init fontconfig */ {
        if (!FcInit()) {
            die("failed to initialize fontconfig");
        }
    }

    /* atoms */ {
        atoms[A_Cardinal] = XInternAtom(dp, "CARDINAL", False);
        atoms[A_Clipboard] = XInternAtom(dp, "CLIPBOARD", False);
        atoms[A_Targets] = XInternAtom(dp, "TARGETS", False);
        atoms[A_Utf8string] = XInternAtom(dp, "UTF8_STRING", False);
        atoms[A_ImagePng] = XInternAtom(dp, "image/png", False);
        atoms[A_TextUriList] = XInternAtom(dp, "text/uri-list", False);
        atoms[A_XSelData] = XInternAtom(dp, "XSEL_DATA", False);
        atoms[A_WmProtocols] = XInternAtom(dp, "WM_PROTOCOLS", False);
        atoms[A_WmDeleteWindow] = XInternAtom(dp, "WM_DELETE_WINDOW", False);
        atoms[A_NetWmSyncRequest] = XInternAtom(dp, "_NET_WM_SYNC_REQUEST", False);
        atoms[A_NetWmSyncRequestCounter] = XInternAtom(dp, "_NET_WM_SYNC_REQUEST_COUNTER", False);
        atoms[A_XDndAware] = XInternAtom(dp, "XdndAware", False);
        atoms[A_XDndPosition] = XInternAtom(dp, "XdndPosition", False);
        atoms[A_XDndSelection] = XInternAtom(dp, "XdndSelection", False);
        atoms[A_XDndStatus] = XInternAtom(dp, "XdndStatus", False);
        atoms[A_XDndActionCopy] = XInternAtom(dp, "XdndActionCopy", False);
        atoms[A_XDndDrop] = XInternAtom(dp, "XdndDrop", False);
    }

    /* xrender */ {
        ctx->dc.sys.xrnd_pic_format = XRenderFindStandardFormat(ctx->dc.dp, PictStandardARGB32);
        assert(ctx->dc.sys.xrnd_pic_format);
    }

    i32 screen = DefaultScreen(dp);
    Window root = DefaultRootWindow(dp);
    i32 const depth = 32;

    if (!XMatchVisualInfo(dp, screen, depth, TrueColor, &ctx->dc.sys.vinfo)) {
        die("can't get visual information for screen");
    }
    if (ctx->dc.sys.vinfo.depth != depth) {
        die("unexpected screen depth (%d)", ctx->dc.sys.vinfo.depth);
    }

    ctx->dc.sys.colmap = XCreateColormap(dp, root, ctx->dc.sys.vinfo.visual, AllocNone);

    /* create window */
    ctx->dc.window = XCreateWindow(
        dp,
        root,
        0,
        0,
        ctx->dc.width,
        ctx->dc.height,
        0,  // border width
        ctx->dc.sys.vinfo.depth,
        InputOutput,
        ctx->dc.sys.vinfo.visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWEventMask,
        &(XSetWindowAttributes) {.colormap = ctx->dc.sys.colmap,
                                 .border_pixel = 0,
                                 .background_pixel = WND_BACKGROUND,
                                 .event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask | ExposureMask
                                     | PointerMotionMask | StructureNotifyMask}
    );
    ctx->dc.screen_gc = XCreateGC(dp, ctx->dc.window, 0, 0);

    XSetWMName(
        dp,
        ctx->dc.window,
        &(XTextProperty
        ) {.value = (unsigned char*)title, .nitems = strlen(title), .format = 8, .encoding = atoms[A_Utf8string]}
    );

    /* turn on protocol support */ {
        Atom wm_delete_window = XInternAtom(dp, "WM_DELETE_WINDOW", False);

        Atom protocols[] = {wm_delete_window, atoms[A_NetWmSyncRequest]};
        XSetWMProtocols(dp, ctx->dc.window, protocols, LENGTH(protocols));

        xwindow_set_cardinal(dp, ctx->dc.window, atoms[A_XDndAware], 5);
    }

    /* _NET_WM_SYNC_REQUEST */ {
        XSyncIntToValue(&ctx->xsync.last_request_value, 0);
        ctx->xsync.counter = XSyncCreateCounter(dp, ctx->xsync.last_request_value);

        XChangeProperty(
            dp,
            ctx->dc.window,
            atoms[A_NetWmSyncRequestCounter],
            XA_CARDINAL,
            32,
            PropModeReplace,
            (unsigned char*)&ctx->xsync.counter,
            1
        );
    }

    /* X input method setup */ {
        // from https://gist.github.com/baines/5a49f1334281b2685af5dcae81a6fa8a
        XSetLocaleModifiers("");

        ctx->dc.sys.xim = XOpenIM(dp, 0, 0, 0);
        if (!ctx->dc.sys.xim) {
            // fallback to internal input method
            XSetLocaleModifiers("@im=none");
            ctx->dc.sys.xim = XOpenIM(dp, 0, 0, 0);
        }

        ctx->dc.sys.xic = XCreateIC(
            ctx->dc.sys.xim,
            XNInputStyle,
            XIMPreeditNothing | XIMStatusNothing,
            XNClientWindow,
            ctx->dc.window,
            XNFocusWindow,
            ctx->dc.window,
            NULL
        );
        XSetICFocus(ctx->dc.sys.xic);
    }

    ctx->dc.back_buffer = XdbeAllocateBackBufferName(dp, ctx->dc.window, 0);

    if (!xft_font_set(&ctx->dc, UI_FONT_NAME, &ctx->dc.fnt)) {
        die("failed to load default ui font: %s", UI_FONT_NAME);
    }

    /* tc */ {
        for (u32 i = 0; i < TCS_NUM; ++i) {
            arrpush(ctx->tcarr, tc_new(&ctx->dc));
        }
    }

    /* schemes */ {
        ctx->dc.schemes_dyn = ecalloc(SchmLast, sizeof(ctx->dc.schemes_dyn[0]));
        for (i32 i = 0; i < SchmLast; ++i) {
            for (i32 j = 0; j < 2; ++j) {
                if (!XftColorAllocValue(
                        dp,
                        ctx->dc.sys.vinfo.visual,
                        ctx->dc.sys.colmap,
                        &SCHEMES[i][j],
                        j ? &ctx->dc.schemes_dyn[i].bg : &ctx->dc.schemes_dyn[i].fg
                    )) {
                    die("can't alloc color");
                };
            }
        }
    }

    /* static images */ {
        for (i32 i = 0; i < I_Last; ++i) {
            struct IconData icon = get_icon_data(i);
            if (icon.data) {
                struct Image im = read_file_from_memory(&ctx->dc, icon.data, icon.len, COL_BG(&ctx->dc, SchmNorm));
                images[i] = im.im;
            }
        }
    }

    /* canvas */ {
        XGCValues canvas_gc_vals =
            {.line_style = LineSolid, .line_width = 5, .cap_style = CapButt, .fill_style = FillSolid};
        ctx->dc.gc = XCreateGC(
            dp,
            ctx->dc.window,
            GCForeground | GCBackground | GCFillStyle | GCLineStyle | GCLineWidth | GCCapStyle | GCJoinStyle,
            &canvas_gc_vals
        );
        // read canvas data from file or create empty
        if (ctx->inp.t != IO_None) {
            struct Image im = read_image_io(&ctx->dc, &ctx->inp, 0);
            if (!canvas_load(ctx, &im)) {
                die("failed to read input file '%s'", ioctx_as_str(&ctx->inp));
            }
        } else {
            Pixmap data = XCreatePixmap(dp, ctx->dc.window, ctx->dc.width, ctx->dc.height, ctx->dc.sys.vinfo.depth);
            ctx->dc.cv.im = XGetImage(dp, data, 0, 0, ctx->dc.width, ctx->dc.height, AllPlanes, ZPixmap);
            XFreePixmap(dp, data);
            // initial canvas color
            canvas_fill(ctx->dc.cv.im, CANVAS_BACKGROUND);
        }
        /* overlay */ {
            ctx->input.ovr.im = XSubImage(ctx->dc.cv.im, 0, 0, ctx->dc.cv.im->width, ctx->dc.cv.im->height);
            overlay_clear(&ctx->input.ovr);
        }

        ctx->dc.width = CLAMP(ctx->dc.cv.im->width, WND_LAUNCH_MIN_SIZE.x, WND_LAUNCH_MAX_SIZE.x);
        ctx->dc.height = CLAMP(
            ctx->dc.cv.im->height + (i32)statusline_height(&ctx->dc),
            WND_LAUNCH_MIN_SIZE.y,
            WND_LAUNCH_MAX_SIZE.y
        );
        XResizeWindow(dp, ctx->dc.window, ctx->dc.width, ctx->dc.height);
    }

    // draw cache
    dc_cache_init(ctx);

    for (u32 i = 0; i < TCS_NUM; ++i) {
        tc_set_tool(&ctx->tcarr[i], Tool_Pencil);
    }

    update_screen(ctx, PNIL, True);

    /* show up window */
    XMapRaised(dp, ctx->dc.window);
}

void run(struct Ctx* ctx) {
    static HdlrResult (*const handlers[LASTEvent])(struct Ctx*, XEvent*) = {
        [KeyPress] = &key_press_hdlr,
        [ButtonPress] = &button_press_hdlr,
        [ButtonRelease] = &button_release_hdlr,
        [MotionNotify] = &motion_notify_hdlr,
        [Expose] = &expose_hdlr,
        [DestroyNotify] = &destroy_notify_hdlr,
        [ConfigureNotify] = &configure_notify_hdlr,
        [SelectionRequest] = &selection_request_hdlr,
        [SelectionNotify] = &selection_notify_hdlr,
        [ClientMessage] = &client_message_hdlr,
        [MappingNotify] = &mapping_notify_hdlr,
    };

    HdlrResult running = HR_Ok;
    XEvent event = {0};

    XSync(ctx->dc.dp, False);
    while (running != HR_Quit && !XNextEvent(ctx->dc.dp, &event)) {
        if (XFilterEvent(&event, ctx->dc.window)) {
            continue;
        }
        if (handlers[event.type]) {
            running = handlers[event.type](ctx, &event);
        }
    }
}

HdlrResult button_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct Input* inp = &ctx->input;
    Button const button = get_btn(e);

    inp->damage = RNIL;
    inp->redraw_track[0] = RNIL;
    inp->redraw_track[1] = RNIL;

    if (inp->mode.t == InputT_Transform || inp->mode.t == InputT_Text) {
        // do nothing
    } else if (inp->c.state == CS_None && (btn_eq(button, BTN_SEL_CIRC) | btn_eq(button, BTN_SEL_CIRC_ALTERNATIVE))) {
        sel_circ_init_and_show(ctx, button, e->x, e->y);
    } else if (tc->on_press) {
        Rect const curr_damage = tc->on_press(ctx, e);
        overlay_expand_rect(&inp->ovr, curr_damage);
        input_set_damage(inp, curr_damage);
    }

    update_screen(ctx, (Pair) {e->x, e->y}, False);
    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    inp->c = (struct CursorState) {
        .state = CS_Hold,
        .btn = button,
        .pos = point_from_scr_to_cv_xy(&ctx->dc, e->x, e->y),
    };

    return HR_Ok;
}

HdlrResult button_release_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;
    Button const e_btn = get_btn(e);

    if (btn_eq(e_btn, BTN_SCROLL_UP)) {
        canvas_scroll(&dc->cv, (Pair) {0, 10});
    } else if (btn_eq(e_btn, BTN_SCROLL_DOWN)) {
        canvas_scroll(&dc->cv, (Pair) {0, -10});
    } else if (btn_eq(e_btn, BTN_SCROLL_LEFT)) {
        canvas_scroll(&dc->cv, (Pair) {-10, 0});
    } else if (btn_eq(e_btn, BTN_SCROLL_RIGHT)) {
        canvas_scroll(&dc->cv, (Pair) {10, 0});
    } else if (btn_eq(e_btn, BTN_ZOOM_IN)) {
        canvas_change_zoom(dc, inp->prev_c, 1);
    } else if (btn_eq(e_btn, BTN_ZOOM_OUT)) {
        canvas_change_zoom(dc, inp->prev_c, -1);
    } else if (inp->mode.t == InputT_Transform) {
        struct InputTransformData* transd = &inp->mode.d.trans;
        transd->acc = trans_add(transd->acc, transd->curr);
        transd->curr = TRANSFORM_DEFAULT;
    } else if (inp->mode.t == InputT_Text) {
        // do not run tool handlers
    } else if (btn_eq(e_btn, BTN_CANVAS_RESIZE) && inp->mode.t == InputT_Interact) {
        Pair const cur = point_from_scr_to_cv_xy(dc, e->x, e->y);
        canvas_resize(ctx, cur.x, cur.y);
    } else if (btn_eq(e_btn, BTN_SEL_CIRC) || btn_eq(e_btn, BTN_SEL_CIRC_ALTERNATIVE)) {
        i32 const selected_item = sel_circ_curr_item(&ctx->sc, e->x, e->y);
        if (selected_item != NIL && ctx->sc.items_arr[selected_item].on_select) {
            struct Item* item = &ctx->sc.items_arr[selected_item];
            item->on_select(ctx, item->arg);
        }
    } else if (tc->on_release) {
        Rect const curr_damage = tc->on_release(ctx, e);
        overlay_expand_rect(&inp->ovr, curr_damage);

        Rect final_damage = rect_expand(inp->damage, curr_damage);
        if (!IS_RNIL(final_damage)) {
            input_set_damage(inp, final_damage);
            history_forward(ctx, history_new_item(dc->cv.im, final_damage));
            ximage_blend(dc->cv.im, inp->ovr.im);
            overlay_clear(&inp->ovr);
        }

        update_screen(ctx, (Pair) {e->x, e->y}, False);
        input_set_damage(inp, RNIL);
    }

    inp->c = (struct CursorState) {0};

    sel_circ_free_and_hide(&ctx->sc);
    update_screen(ctx, (Pair) {e->x, e->y}, False);

    return HR_Ok;
}

HdlrResult destroy_notify_hdlr(__attribute__((unused)) struct Ctx* ctx, __attribute__((unused)) XEvent* event) {
    return HR_Ok;
}

HdlrResult expose_hdlr(struct Ctx* ctx, XEvent* event) {
    XExposeEvent* e = (XExposeEvent*)event;

    update_screen(ctx, (Pair) {e->x, e->y}, False);
    return HR_Ok;
}

static void to_next_input_digit(struct Input* input, Bool is_increment) {
    struct InputMode* mode = &input->mode;
    assert(mode->t == InputT_Color);

    if (mode->d.col.current_digit == 0 && !is_increment) {
        mode->d.col.current_digit = 5;
    } else if (mode->d.col.current_digit == 5 && is_increment) {
        mode->d.col.current_digit = 0;
    } else {
        mode->d.col.current_digit += is_increment ? 1 : -1;
    }
}

HdlrResult key_press_hdlr(struct Ctx* ctx, XEvent* event) {
    // FIXME repeated assignments will cause memory leaks
    char* cl_msg_to_show = NULL;

    struct Input* inp = &ctx->input;
    struct InputMode* mode = &inp->mode;

    XKeyPressedEvent e = event->xkey;
    if (e.type == KeyRelease) {
        return HR_Ok;
    }

    Status lookup_status = 0;
    KeySym key_sym = NoSymbol;
    char lookup_buf[32] = {0};
    i32 const text_len =
        Xutf8LookupString(ctx->dc.sys.xic, &e, lookup_buf, sizeof(lookup_buf) - 1, &key_sym, &lookup_status);

    if (lookup_status == XBufferOverflow) {
        trace("xpaint: input buffer overflow");
    }

    Key curr = {
        key_sym,  // works on different languages
        e.state
    };

    // custom conditions
    if (mode->t == InputT_Interact && BETWEEN(curr.sym, XK_1, XK_9)) {
        u32 val = key_sym - XK_1;
        if (val < TCS_NUM) {
            ctx->curr_tc = val;
        }
    }
    if (mode->t == InputT_Interact && BETWEEN(curr.sym, XK_Left, XK_Down)
        && (state_match(curr.mask, ControlMask) || state_match(curr.mask, ControlMask | ShiftMask))) {
        u32 const value = e.state & ShiftMask ? 25 : 5;
        canvas_resize(
            ctx,
            (i32)(ctx->dc.cv.im->width
                  + (key_sym == XK_Left        ? -value
                         : key_sym == XK_Right ? value
                                               : 0)),
            (i32)(ctx->dc.cv.im->height
                  + (key_sym == XK_Down     ? -value
                         : key_sym == XK_Up ? value
                                            : 0))
        );
    }
    // change selected color digit with pressed key
    if (mode->t == InputT_Color && strlen(lookup_buf) == 1) {
        i32 val = lookup_buf[0]
            - (BETWEEN(lookup_buf[0], '0', '9')       ? '0'
                   : BETWEEN(lookup_buf[0], 'a', 'f') ? ('a' - 10)
                   : BETWEEN(lookup_buf[0], 'A', 'F') ? ('A' - 10)
                                                      : 0);
        if (val != lookup_buf[0]) {  // if assigned
            // selected digit counts from left to
            // right except alpha (aarrggbb <=> --012345)
            u32 shift = (5 - mode->d.col.current_digit) * 4;
            *tc_curr_col(&CURR_TC(ctx)) &= ~(0xF << shift);  // clear
            *tc_curr_col(&CURR_TC(ctx)) |= val << shift;  // set
            to_next_input_digit(&ctx->input, True);
        }
    }

    // else-if chain to filter keys
    if (mode->t == InputT_Console) {
        struct InputConsoleData* cl = &ctx->input.mode.d.cl;
        if (key_eq(curr, KEY_CL_MODE_INTERACT)) {
            if (cl->compls_arr) {
                cl_compls_free(cl);
            } else {
                input_mode_set(ctx, InputT_Interact);
            }
        } else if (key_eq(curr, KEY_CL_CLIPBOARD_PASTE)) {
            trigger_clipboard_paste(&ctx->dc, atoms[A_Utf8string]);
            // handled in selection_notify_hdlr
        } else if (key_eq(curr, KEY_CL_APPLY_COMPLT) && cl->compls_arr) {
            char* complt = cl->compls_arr[cl->compls_curr].val_dyn;
            while (*complt) {
                arrpush(cl->cmdarr, *complt);
                complt += 1;
            }
            cl_compls_free(cl);
            // FIXME add contidion
            if (!cl->dont_append_delimeter_after_apply) {
                cl_push(cl, CL_DELIM[0]);
            }
        } else if (key_eq(curr, KEY_CL_RUN)) {  // run command
            char* cmd_dyn = cl_cmd_get_str_dyn(cl);
            input_mode_set(ctx, InputT_Interact);
            ClCPrsResult parse_res = cl_cmd_parse(ctx, cmd_dyn);
            str_free(&cmd_dyn);
            Bool is_exit = False;
            switch (parse_res.t) {
                case ClCPrs_Ok: {
                    struct ClCommand* cmd = &parse_res.d.ok;
                    ClCPrcResult proc_res = cl_cmd_process(ctx, cmd);
                    if (proc_res.bit_status & ClCPrc_Msg) {
                        // XXX double memory allocation
                        cl_msg_to_show = str_new(proc_res.msg_dyn);
                        str_free(&proc_res.msg_dyn);  // XXX member free
                    }
                    is_exit = (Bool)(proc_res.bit_status & ClCPrc_Exit);
                } break;
                case ClCPrs_ENoArg: {
                    if (parse_res.d.noarg.context_optdyn) {
                        cl_msg_to_show = str_new(
                            "provide %s to '%s' command",
                            parse_res.d.noarg.arg_desc_dyn,
                            parse_res.d.noarg.context_optdyn
                        );
                    } else {
                        cl_msg_to_show = str_new("provide %s", parse_res.d.noarg.arg_desc_dyn);
                    }
                } break;
                case ClCPrs_EInvArg: {
                    if (parse_res.d.invarg.context_optdyn) {
                        cl_msg_to_show = str_new(
                            "%s: invalid arg '%s': %s",
                            parse_res.d.invarg.context_optdyn,
                            parse_res.d.invarg.arg_dyn,
                            parse_res.d.invarg.error_dyn
                        );
                    } else {
                        cl_msg_to_show =
                            str_new("invalid arg '%s': %s", parse_res.d.invarg.arg_dyn, parse_res.d.invarg.error_dyn);
                    }
                } break;
            }

            cl_cmd_parse_res_free(&parse_res);
            if (is_exit) {
                return HR_Quit;
            }
        } else if (key_eq(curr, KEY_CL_REQ_COMPLT) && !cl->compls_arr) {
            cl_compls_new(cl);
        } else if (key_eq(curr, KEY_CL_PREV_COMPLT) && cl->compls_arr) {
            usize len = (usize)arrlen(cl->compls_arr);
            usize last = len ? len - 1 : 0;
            cl->compls_curr = cl->compls_curr == 0 ? last : cl->compls_curr - 1;
        } else if (key_eq(curr, KEY_CL_NEXT_COMPLT) && cl->compls_arr) {
            usize max = arrlen(cl->compls_arr);
            if (max) {
                cl->compls_curr = (cl->compls_curr + 1) % max;
            }
        } else if (key_eq(curr, KEY_CL_ERASE_ALL)) {
            usize is_not_infinite_loop = 100000;
            while (cl_pop(cl) && --is_not_infinite_loop) {
                // empty body
            }
        } else if (key_eq(curr, KEY_CL_ERASE_CHAR)) {
            cl_pop(cl);
        } else if (!(iscntrl((u32)*lookup_buf)) && (lookup_status == XLookupBoth || lookup_status == XLookupChars)) {
            for (i32 i = 0; i < text_len; ++i) {
                cl_push(cl, (char)(lookup_buf[i] & 0xFF));
            }
        }
    }

    // actions
    if (can_action(inp, curr, ACT_UNDO)) {
        if (!history_move(ctx, False)) {
            trace("xpaint: can't undo history");
        }
    }
    if (can_action(inp, curr, ACT_REVERT)) {
        if (!history_move(ctx, True)) {
            trace("xpaint: can't revert history");
        }
    }
    if (can_action(inp, curr, ACT_PASTE_IMAGE)) {
        switch (mode->t) {
            case InputT_Interact:
                trigger_clipboard_paste(&ctx->dc, atoms[A_ImagePng]);
                // handled in selection_notify_hdlr
                break;
            case InputT_Text:
            case InputT_Color:
            case InputT_Console:
                trigger_clipboard_paste(&ctx->dc, atoms[A_Utf8string]);
                // handled in selection_notify_hdlr
                break;
            case InputT_Transform: trace("xpaint: can't paste in transform mode"); break;
        }
    }
    if (can_action(inp, curr, ACT_COPY_AREA)) {
        XSetSelectionOwner(ctx->dc.dp, atoms[A_Clipboard], ctx->dc.window, CurrentTime);

        if (ctx->sel_buf.im != NULL) {
            XDestroyImage(ctx->sel_buf.im);
        }

        if (IS_RNIL(inp->ovr.rect)) {
            // copy all canvas
            i32 const w = ctx->dc.cv.im->width;
            i32 const h = ctx->dc.cv.im->height;
            ctx->sel_buf.im = XSubImage(ctx->dc.cv.im, 0, 0, w, h);
        } else {
            // copy overlay
            struct InputOverlay transformed = get_transformed_overlay(&ctx->dc, inp);
            Pair const dims = rect_dims(transformed.rect);
            ctx->sel_buf.im = XSubImage(transformed.im, transformed.rect.l, transformed.rect.t, dims.x, dims.y);
            overlay_free(&transformed);
        }

        assert(ctx->sel_buf.im != NULL);
    }
    if (can_action(inp, curr, ACT_SWAP_COLOR)) {
        tc_set_curr_col_num(&CURR_TC(ctx), CURR_TC(ctx).prev_col);
    }
    if (can_action(inp, curr, ACT_ZOOM_IN)) {
        canvas_change_zoom(&ctx->dc, ctx->input.prev_c, 1);
    }
    if (can_action(inp, curr, ACT_ZOOM_OUT)) {
        canvas_change_zoom(&ctx->dc, ctx->input.prev_c, -1);
    }
    if (can_action(inp, curr, ACT_MODE_INTERACT)) {
        input_mode_set(ctx, InputT_Interact);
    }
    if (can_action(inp, curr, ACT_MODE_COLOR)) {
        input_mode_set(ctx, InputT_Color);
    }
    if (can_action(inp, curr, ACT_MODE_CONSOLE)) {
        input_mode_set(ctx, InputT_Console);
    }
    if (can_action(inp, curr, ACT_ADD_COLOR)) {
        u32 const len = arrlen(CURR_TC(ctx).colarr);
        if (len != MAX_COLORS) {
            tc_set_curr_col_num(&CURR_TC(ctx), len);
            arrpush(CURR_TC(ctx).colarr, 0xFF000000);
        }
    }
    if (can_action(inp, curr, ACT_TO_RIGHT_COL_DIGIT)) {
        to_next_input_digit(&ctx->input, True);
    }
    if (can_action(inp, curr, ACT_TO_LEFT_COL_DIGIT)) {
        to_next_input_digit(&ctx->input, False);
    }
    if (can_action(inp, curr, ACT_EXIT)) {
        return HR_Quit;
    }
    if (can_action(inp, curr, ACT_NEXT_COLOR)) {
        u32 const curr_col = CURR_TC(ctx).curr_col;
        u32 const col_num = arrlen(CURR_TC(ctx).colarr);
        tc_set_curr_col_num(&CURR_TC(ctx), curr_col + 1 == col_num ? 0 : curr_col + 1);
    }
    if (can_action(inp, curr, ACT_PREV_COLOR)) {
        u32 const curr_col = CURR_TC(ctx).curr_col;
        u32 const col_num = arrlen(CURR_TC(ctx).colarr);
        assert(col_num != 0);
        tc_set_curr_col_num(&CURR_TC(ctx), curr_col == 0 ? col_num - 1 : curr_col - 1);
    }
    if (can_action(inp, curr, ACT_SAVE_TO_FILE)) {  // save to current file
        if (write_io(&ctx->dc, inp, ctx->dc.cv.type, &ctx->out)) {
            cl_msg_to_show = str_new("changes saved");
        } else {
            cl_msg_to_show = str_new("failed to save changes");
        }
    }

    // else-if chain to filter keys
    if (mode->t == InputT_Text) {
        if (key_eq(curr, KEY_TX_MODE_INTERACT)) {
            input_set_damage(inp, RNIL);
            overlay_clear(&inp->ovr);
            input_mode_set(ctx, InputT_Interact);
        } else if (key_eq(curr, KEY_TX_CONFIRM)) {
            input_mode_set(ctx, InputT_Transform);
        } else if (key_eq(curr, KEY_TX_PASTE_TEXT)) {
            trigger_clipboard_paste(&ctx->dc, atoms[A_Utf8string]);
        } else if (key_eq(curr, KEY_TX_ERASE_ALL)) {
            usize is_not_infinite_loop = 100000;
            while (text_mode_pop(ctx) && --is_not_infinite_loop) {
                // empty body
            }
        } else if (key_eq(curr, KEY_TX_ERASE_CHAR)) {
            text_mode_pop(ctx);
        }
        // FIXME add way to type (and render) '\n'?
        else if (!(iscntrl((u32)*lookup_buf)) && (lookup_status == XLookupBoth || lookup_status == XLookupChars)) {
            for (i32 i = 0; i < text_len; ++i) {
                text_mode_push(ctx, (char)(lookup_buf[i] & 0xFF));
            }
        }
        // FIXME do full screen update?
        update_screen(ctx, PNIL, True);
    }

    // FIXME extra updates on invalid events
    update_screen(ctx, (Pair) {e.x, e.y}, True);
    if (cl_msg_to_show) {
        show_message(ctx, cl_msg_to_show);
        str_free(&cl_msg_to_show);
    }

    return HR_Ok;
}

HdlrResult mapping_notify_hdlr(__attribute__((unused)) struct Ctx* ctx, XEvent* event) {
    XRefreshKeyboardMapping(&event->xmapping);
    return HR_Ok;
}

HdlrResult motion_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XMotionEvent* e = (XMotionEvent*)event;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct Input* inp = &ctx->input;
    Pair const cur = point_from_scr_to_cv_xy(&ctx->dc, e->x, e->y);

    if (ctx->input.c.state != CS_None) {
        if (ctx->input.c.state != CS_Drag && !PAIR_EQ(cur, ctx->input.c.pos)) {
            ctx->input.c.state = CS_Drag;
        }

        struct timeval current_time;
        gettimeofday(&current_time, 0x0);
        // XXX mouse scroll and drag event are shared
        u64 const elapsed_from_last = current_time.tv_usec - ctx->input.last_proc_drag_ev_us;

        if (btn_eq(inp->c.btn, BTN_SCROLL_DRAG)) {
            canvas_scroll(&ctx->dc.cv, (Pair) {e->x - inp->prev_c.x, e->y - inp->prev_c.y});
            // last update will be in button_release_hdlr
            if (elapsed_from_last >= MOUSE_SCROLL_UPDATE_PERIOD_US) {
                ctx->input.last_proc_drag_ev_us = current_time.tv_usec;
                update_screen(ctx, (Pair) {e->x, e->y}, False);
            }
        } else if (elapsed_from_last >= DRAG_EVENT_PROC_PERIOD_US) {
            if (inp->mode.t == InputT_Transform) {
                struct InputTransformData* transd = &inp->mode.d.trans;
                Pair const cur_delta = {cur.x - inp->c.pos.x, cur.y - inp->c.pos.y};

                if (btn_eq(inp->c.btn, BTN_TRANS_SCALE) || btn_eq(inp->c.btn, BTN_TRANS_SCALE_UNIFORM)) {
                    // adjust scale to match cursor

                    Transform trans = transd->acc;
                    Pair lt = {inp->ovr.rect.l, inp->ovr.rect.t};
                    Pair rb = {inp->ovr.rect.r, inp->ovr.rect.b};
                    Pair lt_trans = point_apply_trans_pivot(lt, trans, lt);
                    Pair rb_trans = point_apply_trans_pivot(rb, trans, lt);
                    Pair delta = {cur.x - rb_trans.x, cur.y - rb_trans.y};
                    Pair dims = {rb_trans.x - lt_trans.x, rb_trans.y - lt_trans.y};
                    DPt scale = {((double)dims.x + delta.x) / dims.x, ((double)dims.y + delta.y) / dims.y};

                    transd->curr.scale = btn_eq(inp->c.btn, BTN_TRANS_SCALE)
                        ? scale
                        : (DPt) {MAX(scale.x, scale.y), MAX(scale.x, scale.y)};
                } else if (btn_eq(inp->c.btn, BTN_TRANS_ROTATE) | btn_eq(inp->c.btn, BTN_TRANS_ROTATE_SNAP)) {
                    double const snap_interval = PI / 4;  // 45 degrees in radians

                    double angle_delta = cur_delta.y * TFM_MODE_ROTATE_SENSITIVITY;

                    if (btn_eq(inp->c.btn, BTN_TRANS_ROTATE_SNAP)) {
                        // Snap to nearest 45° increment relative to accumulated rotation
                        double const total_angle = transd->acc.rotate + angle_delta;
                        double const snapped_total = round(total_angle / snap_interval) * snap_interval;
                        angle_delta = snapped_total - transd->acc.rotate;
                    }

                    transd->curr.rotate = angle_delta;
                } else if (btn_eq(inp->c.btn, BTN_TRANS_MOVE)) {
                    transd->curr.move = cur_delta;
                } else if (btn_eq(inp->c.btn, BTN_TRANS_MOVE_LOCK)) {
                    Pair snapped = cur_delta;
                    double const angle_threshold = 0.4142;  // tan(22.5°)
                    i32 const min_move = 2;  // Minimum pixels to trigger snapping

                    double const dx = snapped.x;
                    double const dy = snapped.y;
                    double const adx = fabs(dx);
                    double const ady = fabs(dy);

                    if (adx + ady >= min_move) {
                        i32 const sx = dx < 0 ? -1 : 1;
                        i32 const sy = dy < 0 ? -1 : 1;
                        double const max_ad = fmax(adx, ady);
                        double const ratio = fmin(adx, ady) / max_ad;

                        if (ratio < angle_threshold) {
                            // Axis lock
                            snapped = (adx > ady) ? (Pair) {sx * (int)max_ad, 0} : (Pair) {0, sy * (int)max_ad};
                        } else {
                            // Diagonal lock (45°)
                            i32 const mag = (int)round(max_ad);
                            snapped = (Pair) {sx * mag, sy * mag};
                        }
                    }

                    transd->curr.move = snapped;
                }

                update_screen(ctx, (Pair) {e->x, e->y}, False);
            } else if (tc->on_drag) {
                Rect curr_damage = tc->on_drag(ctx, e);
                overlay_expand_rect(&inp->ovr, curr_damage);

                if (!IS_RNIL(curr_damage)) {
                    input_set_damage(inp, rect_expand(inp->damage, curr_damage));
                    inp->last_proc_drag_ev_us = current_time.tv_usec;
                }
                // FIXME it here to draw selection tool (returns RNIL in on_drag)
                update_screen(ctx, (Pair) {e->x, e->y}, False);
            }
        }
    } else {
        // FIXME unused
        if (tc->on_move) {
            Rect curr_damage = tc->on_move(ctx, e);
            overlay_expand_rect(&inp->ovr, curr_damage);

            if (!IS_RNIL(curr_damage)) {
                input_set_damage(inp, rect_expand(inp->damage, curr_damage));
                inp->last_proc_drag_ev_us = 0;
                update_screen(ctx, (Pair) {e->x, e->y}, False);
            }
        }
    }

    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    inp->prev_c.x = e->x;
    inp->prev_c.y = e->y;

    return HR_Ok;
}

HdlrResult configure_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XConfigureEvent const e = event->xconfigure;
    struct DrawCtx* dc = &ctx->dc;

    if ((i32)dc->width == e.width && (i32)dc->height == e.height) {
        // configure notify calls on move events too
        return HR_Ok;
    }

    dc->width = e.width;
    dc->height = e.height;
    // backbuffer resizes automatically

    Pair const cv_size = canvas_size(dc);
    Pair const clientarea = clientarea_size(dc);

    // if canvas fits in client area
    if (cv_size.x <= clientarea.x && cv_size.y <= clientarea.y) {
        // place canvas to center of screen
        dc->cv.scroll.x = (clientarea.x - cv_size.x) / 2;
        dc->cv.scroll.y = (clientarea.y - cv_size.y) / 2;
    }

    return HR_Ok;
}

HdlrResult selection_request_hdlr(struct Ctx* ctx, XEvent* event) {
    XSelectionRequestEvent request = event->xselectionrequest;

    if (XGetSelectionOwner(ctx->dc.dp, atoms[A_Clipboard]) != ctx->dc.window || request.selection != atoms[A_Clipboard]
        || request.property == None) {
        return HR_Ok;
    }

    if (request.target == atoms[A_Targets]) {
        Atom available_targets[] = {atoms[A_ImagePng]};
        XChangeProperty(
            request.display,
            request.requestor,
            request.property,
            XA_ATOM,
            32,
            PropModeReplace,
            (unsigned char const*)available_targets,
            LENGTH(available_targets)
        );
    } else if (request.target == atoms[A_ImagePng]) {
        u8* rgb_dyn = ximage_to_rgb(ctx->sel_buf.im, False);
        i32 png_data_size = NIL;
        stbi_uc* png_imdyn =
            stbi_write_png_to_mem(rgb_dyn, 0, ctx->sel_buf.im->width, ctx->sel_buf.im->height, 3, &png_data_size);
        if (png_imdyn != NULL) {
            XChangeProperty(
                request.display,
                request.requestor,
                request.property,
                request.target,
                8,
                PropModeReplace,
                png_imdyn,
                png_data_size
            );
        } else {
            trace("selection request handler: stbi error: %s", stbi_failure_reason());
        }

        free(rgb_dyn);
        stbi_image_free(png_imdyn);
    }

    XSelectionEvent sendEvent = {
        .type = SelectionNotify,
        .serial = request.serial,
        .send_event = request.send_event,
        .display = request.display,
        .requestor = request.requestor,
        .selection = request.selection,
        .target = request.target,
        .property = request.property,
        .time = request.time,
    };
    XSendEvent(ctx->dc.dp, request.requestor, 0, 0, (XEvent*)&sendEvent);

    return HR_Ok;
}

static void copy_image_to_transform_mode(struct Ctx* ctx, XImage* im) {
    struct InputOverlay* ovr = &ctx->input.ovr;

    overlay_clear(ovr);
    ximage_blend(ovr->im, im);
    ovr->rect = ximage_calc_damage(ovr->im);
    input_set_damage(&ctx->input, ovr->rect);
    input_mode_set(ctx, InputT_Transform);
}

HdlrResult selection_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    struct DrawCtx* dc = &ctx->dc;
    struct ToolCtx* tc = &CURR_TC(ctx);

    XSelectionEvent e = event->xselection;

    if (e.requestor != dc->window || e.property == None) {
        return HR_Ok;
    }

    Atom actual_type = 0;
    i32 actual_format = 0;
    u64 bytes_after = 0;
    unsigned char* data_xdyn = NULL;
    u64 count = 0;
    XGetWindowProperty(
        dc->dp,
        e.requestor,
        e.property,
        0,
        LONG_MAX,
        False,
        AnyPropertyType,
        &actual_type,
        &actual_format,
        &count,
        &bytes_after,
        &data_xdyn
    );

    if (e.target == atoms[A_ImagePng]) {
        XImage* im = read_file_from_memory(dc, data_xdyn, count, 0x00000000).im;
        if (im) {
            copy_image_to_transform_mode(ctx, im);
            update_screen(ctx, PNIL, False);
            XDestroyImage(im);
        } else {
            show_message(ctx, "failed to parse pasted image");
        }
        XDeleteProperty(dc->dp, e.requestor, e.property);
    } else if (e.target == atoms[A_TextUriList]) {
        // last symbols always '\r\n' in valid uri-list
        if (count >= 2 && data_xdyn[count - 2] == '\r') {
            data_xdyn[count - 1] = '\0';
            data_xdyn[count - 2] = '\0';
        }
        for (u32 i = 0; i < count; ++i) {
            if (data_xdyn[i] == '\r' || data_xdyn[i] == '\n') {
                data_xdyn[i] = '\0';
                trace("xpaint: drag&drop only supports 1 file at a time");
                break;
            }
        }

        struct IOCtx ioctx = ioctx_new((char const*)data_xdyn);
        XImage* im = read_image_io(dc, &ioctx, 0x00000000).im;
        ioctx_free(&ioctx);

        if (im) {
            copy_image_to_transform_mode(ctx, im);
            update_screen(ctx, PNIL, False);
            XDestroyImage(im);
        } else {
            show_message(ctx, "failed to parse dragged image");
        }
        XDeleteProperty(dc->dp, e.requestor, e.property);
    } else if (e.target == atoms[A_Utf8string]) {
        switch (ctx->input.mode.t) {
            case InputT_Console:
                for (u32 i = 0; i < count; ++i) {
                    // not letter, because utf-8 is multibyte enc
                    char elem = (char)data_xdyn[i];
                    cl_push(&ctx->input.mode.d.cl, elem);
                }
                update_statusline(ctx);
                break;
            case InputT_Color: {
                if (data_xdyn) {
                    if (argb_from_hex_col((char*)data_xdyn, tc_curr_col(tc))) {
                        update_statusline(ctx);
                    } else {
                        show_message(ctx, "unexpected color format");
                    }
                }
            } break;
                // FIXME combine with InputT_Controle handler?
            case InputT_Text:
                for (u32 i = 0; i < count; ++i) {
                    // not letter, because utf-8 is multibyte enc
                    char elem = (char)data_xdyn[i];
                    text_mode_push(ctx, elem);
                }
                // FIXME calc damage instead?
                update_screen(ctx, PNIL, True);
                break;
            case InputT_Interact:
            case InputT_Transform:
                trace(
                    "xpaint: UTF8_STRING clipboard paste not"
                    " implemented for %s mode",
                    input_mode_as_str(ctx->input.mode.t)
                );
                break;
        }
        XDeleteProperty(dc->dp, e.requestor, e.property);
    }

    if (data_xdyn) {
        XFree(data_xdyn);
    }

    return HR_Ok;
}

HdlrResult client_message_hdlr(struct Ctx* ctx, XEvent* event) {
    struct DrawCtx* dc = &ctx->dc;
    XClientMessageEvent* e = (XClientMessageEvent*)event;
    enum InputTag mode = ctx->input.mode.t;

    if (e->message_type == atoms[A_XDndPosition] && mode == InputT_Interact) {
        XSendEvent(
            dc->dp,
            e->data.l[0],
            False,
            NoEventMask,
            (XEvent*)&(XClientMessageEvent) {
                .type = ClientMessage,
                .window = e->data.l[0],
                .message_type = atoms[A_XDndStatus],
                .format = 32,
                .data.l[0] = (long)dc->window,
                .data.l[1] = 0,  // accept drop
                .data.l[2] = 0,  // no rect
                .data.l[3] = 0,
                .data.l[4] = (long)atoms[A_XDndActionCopy],
            }
        );
    } else if (e->message_type == atoms[A_XDndDrop] && mode == InputT_Interact) {
        XConvertSelection(
            dc->dp,
            atoms[A_XDndSelection],
            atoms[A_TextUriList],
            atoms[A_XDndSelection],
            dc->window,
            CurrentTime
        );
    } else if (e->message_type == atoms[A_WmProtocols]) {
        if ((Atom)e->data.l[0] == atoms[A_WmDeleteWindow]) {
            return HR_Quit;
        }

        if ((Atom)e->data.l[0] == atoms[A_NetWmSyncRequest]) {
            ctx->xsync.last_request_value = (XSyncValue) {.lo = e->data.l[2], .hi = (i32)e->data.l[3]};
            return HR_Ok;
        }
    }

    return HR_Ok;
}

void cleanup(struct Ctx* ctx) {
    /* global */ {
        for (u32 i = 0; i < I_Last; ++i) {
            if (images[i] != NULL) {
                XDestroyImage(images[i]);
            }
        }
    }
    /* sync counter */ { XSyncDestroyCounter(ctx->dc.dp, ctx->xsync.counter); }
    /* file paths */ {
        ioctx_free(&ctx->out);
        ioctx_free(&ctx->inp);
    }
    /* SelectionBuffer */ {
        if (ctx->sel_buf.im != NULL) {
            XDestroyImage(ctx->sel_buf.im);
        }
    }
    /* History */ {
        historyarr_clear(&ctx->hist_nextarr);
        historyarr_clear(&ctx->hist_prevarr);
    }
    /* Selection circle */ { sel_circ_free_and_hide(&ctx->sc); }
    /* ToolCtx */ {
        for (u32 i = 0; i < TCS_NUM; ++i) {
            tc_free(ctx->dc.dp, &ctx->tcarr[i]);
        }
        arrfree(ctx->tcarr);
    }
    /* Input */ { input_free(&ctx->input); }
    /* DrawCtx */ {
        dc_cache_free(&ctx->dc);
        /* Scheme */ {  // depends on VisualInfo and Colormap
            for (i32 i = 0; i < SchmLast; ++i) {
                for (i32 j = 0; j < 2; ++j) {
                    XftColorFree(
                        ctx->dc.dp,
                        ctx->dc.sys.vinfo.visual,
                        ctx->dc.sys.colmap,
                        j ? &ctx->dc.schemes_dyn[i].bg : &ctx->dc.schemes_dyn[i].fg
                    );
                }
            }
            free(ctx->dc.schemes_dyn);
        }
        if (ctx->dc.fnt != NULL) {
            XftFontClose(ctx->dc.dp, ctx->dc.fnt);
        }
        canvas_free(&ctx->dc.cv);
        XdbeDeallocateBackBufferName(ctx->dc.dp, ctx->dc.back_buffer);
        XDestroyIC(ctx->dc.sys.xic);
        XCloseIM(ctx->dc.sys.xim);
        XFreeGC(ctx->dc.dp, ctx->dc.gc);
        XFreeGC(ctx->dc.dp, ctx->dc.screen_gc);
        XFreeColormap(ctx->dc.dp, ctx->dc.sys.colmap);
        XDestroyWindow(ctx->dc.dp, ctx->dc.window);
    }
    /* fontconfig */ { FcFini(); }
}
