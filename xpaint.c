#include <X11/X.h>
#include <X11/Xatom.h>  // XA_*
#include <X11/Xft/Xft.h>
#include <X11/Xft/XftCompat.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdbe.h>  // back buffer
#include <X11/extensions/Xrender.h>
#include <X11/extensions/render.h>
#include <X11/extensions/sync.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/unistd.h>

// libs
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

#include "config.h"
#include "types.h"

// embedded data
INCBIN(u8, pic_tool_fill, "res/tool-fill.png");
INCBIN(u8, pic_tool_pencil, "res/tool-pencil.png");
INCBIN(u8, pic_tool_picker, "res/tool-picker.png");
INCBIN(u8, pic_tool_select, "res/tool-select.png");
INCBIN(u8, pic_tool_brush, "res/tool-brush.png");
INCBIN(u8, pic_tool_figure, "res/tool-figure.png");
INCBIN(u8, pic_fig_rect, "res/figure-rectangle.png");
INCBIN(u8, pic_fig_circ, "res/figure-circle.png");
INCBIN(u8, pic_fig_tri, "res/figure-triangle.png");
INCBIN(u8, pic_fig_fill_on, "res/figure-fill-on.png");
INCBIN(u8, pic_fig_fill_off, "res/figure-fill-off.png");
INCBIN(u8, pic_unknown, "res/unknown.png");

/*
 * -opt vars are nullable (optional)
 * free -dyn vars with 'free' function
 * free -arr vars with 'arrfree' function
 * free -imdyn vars with 'stbi_image_free' function
 * free -xdyn vars with 'XFree' function
 * structs with t and d fields are tagged unions
 */

#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#define CLAMP(X, L, H)   (((X) < (L)) ? (L) : ((X) > (H)) ? (H) : (X))
#define LENGTH(X)        (sizeof(X) / sizeof(X)[0])
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))
#define COALESCE(A, B)   ((A) ? (A) : (B))
#define UNREACHABLE()    __builtin_unreachable()

#define PI               (3.141)
// only one one-byte symbol allowed
#define ARGB_ALPHA       ((argb)(0xFF000000))
#define CL_DELIM         " "
#define IOCTX_STDIO_STR  "-"
// adjust brush line width to pencil's line width
// XXX kinda works only for small values of line_w
#define BRUSH_LINE_W_MOD 4.0

#define CURR_TC(p_ctx)     ((p_ctx)->tcarr[(p_ctx)->curr_tc])
// XXX workaround
#define COL_FG(p_dc, p_sc) ((p_dc)->schemes_dyn[(p_sc)].fg.pixel | 0xFF000000)
#define COL_BG(p_dc, p_sc) ((p_dc)->schemes_dyn[(p_sc)].bg.pixel | 0xFF000000)
#define OVERLAY_TRANSFORM(p_mode) \
    ((p_mode)->t != InputT_Transform \
         ? ((Transform) {0}) \
         : ((Transform) { \
             .move.x = \
                 (p_mode)->d.trans.curr.move.x + (p_mode)->d.trans.acc.move.x, \
             .move.y = \
                 (p_mode)->d.trans.curr.move.y + (p_mode)->d.trans.acc.move.y, \
         }))
#define TC_IS_DRAWER(p_tc) ((p_tc)->t == Tool_Pencil || (p_tc)->t == Tool_Brush)
#define ZOOM_C(p_dc)       (pow(CANVAS_ZOOM_SPEED, (double)(p_dc)->cv.zoom))

enum {
    A_Clipboard,
    A_Targets,
    A_Utf8string,
    A_ImagePng,
    A_WmProtocols,
    A_WmDeleteWindow,
    A_NetWmSyncRequest,
    A_NetWmSyncRequestCounter,
    A_Last,
};

enum Icon {
    I_Select,
    I_Pencil,
    I_Fill,
    I_Picker,
    I_Brush,
    I_Figure,
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
} Transform;

struct Ctx;
struct DrawCtx;
struct ToolCtx;

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
    // FIXME find better place
    XSyncCounter sync_counter;
    XSyncValue last_sync_request_value;

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
        struct Fnt {
            XftFont* xfont;
            u32 h;
        } fnt;
        struct Scheme {
            XftColor fg;
            XftColor bg;
        }* schemes_dyn;  // must be len of SchmLast
        struct Cache {
            u32 pm_w;  // to validate pm
            u32 pm_h;
            Pixmap pm;  // pixel buffer to update screen
            Pixmap overlay;  // extra pixmap for overlay
        } cache;
    } dc;

    struct Input {
        Pair prev_c;
        u64 last_proc_drag_ev_us;

        Pair anchor;

        Bool is_holding;
        Button holding_button;

        Bool is_dragging;
        Pair press_pt;

        i32 png_compression_level;  // FIXME find better place
        i32 jpg_quality_level;  // FIXME find better place

        // drawn on top of canvas
        // clears before *on_press callbacks
        // dumps to main canvas after *on_release callbacks
        XImage* overlay;

        // tracks damage to overlay from _on_press to _on_release.
        Rect damage;

        struct InputMode {
            enum InputTag {
                InputT_Interact,
                InputT_Color,
                InputT_Console,
                InputT_Transform,
            } t;
            union {
                struct InputColorData {
                    u32 current_digit;
                } col;
                struct InputConsoleData {
                    char* cmdarr;
                    char** compls_arr;
                    Bool compls_valid;
                    usize compls_curr;
                } cl;
                struct InputTransformData {
                    Transform acc;  // accumulated
                    Transform curr;
                    Rect overlay_bounds;
                } trans;
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

        enum ToolTag {
            Tool_Selection,
            Tool_Pencil,
            Tool_Fill,
            Tool_Picker,
            Tool_Brush,
            Tool_Figure,
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
        } d;
    }* tcarr;
    u32 curr_tc;

    struct HistItem {
        Pair pivot;  // top left corner position
        XImage* patch;  // changed canvas part
    } *hist_prevarr, *hist_nextarr;

    struct SelectionCircle {
        Bool is_active;
        i32 x;
        i32 y;
        struct Item {
            void (*on_select)(struct Ctx*);
            enum Icon icon;
        }* items_arr;
    } sc;

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
        ClC_Load,
        ClC_Last,
    } t;
    union ClCData {
        struct ClCDSet {
            enum ClCDSTag {
                ClCDS_LineW = 0,
                ClCDS_Col,
                ClCDS_Font,
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
                } font;
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
static usize ximage_data_len(XImage const* im);
static void ximage_apply_trans(XImage** im_out, Transform t);
static Rect ximage_calc_damage(XImage* im);
static Rect rect_expand(Rect a, Rect b);

static XTransform xtrans_scale(double z);
static XTransform xtrans_from_trans(Transform trans);
static XTransform xtrans_mult(XTransform a, XTransform b);

// needs to be 'free'd after use
static char* str_new(char const* fmt, ...);
static char* str_new_va(char const* fmt, va_list args);
static void str_free(char** str_dyn);

static void tc_set_curr_col_num(struct ToolCtx* tc, u32 value);
static argb* tc_curr_col(struct ToolCtx* tc);
static void tc_set_tool(struct ToolCtx* tc, enum ToolTag type);
static char const* tc_get_tool_name(struct ToolCtx const* tc);
static void tc_free(struct ToolCtx* tc);

static Bool fnt_set(struct DrawCtx* dc, char const* font_name);
static void fnt_free(Display* dp, struct Fnt* fnt);
static struct IOCtx ioctx_new(char const* input);
static void ioctx_set(struct IOCtx* ioctx, char const* input);
static char const* ioctx_as_str(struct IOCtx const* ioctx);
static void ioctx_free(struct IOCtx* ioctx);

static Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p);
static Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Bool point_in_rect(Pair p, Pair a1, Pair a2);
static Pair point_apply_trans(Pair p, Transform trans);
static Pair dpt_to_pt(DPt p);
static DPt dpt_rotate(DPt p, double deg); // clockwise
static DPt dpt_add(DPt a, DPt b);

static enum ImageType file_type(u8 const* data, u32 len);
static u8* ximage_to_rgb(XImage const* image, Bool rgba);
// coefficient c is proportional to the significance of component a
static argb argb_blend(argb a, argb b, u8 c);
// receives premultiplied argb value
static argb argb_normalize(argb c);
static struct Image read_file_from_memory(struct DrawCtx const* dc, u8 const* data, u32 len, argb bg);
static struct Image read_image_io(struct DrawCtx const* dc, struct IOCtx const* ioctx, argb bg);
static Bool write_io(struct DrawCtx* dc, struct Input const* input, enum ImageType type, struct IOCtx const* ioctx);

static ClCPrcResult cl_cmd_process(struct Ctx* ctx, struct ClCommand const* cl_cmd);
static ClCPrsResult cl_cmd_parse(struct Ctx* ctx, char const* cl);
static ClCPrsResult cl_prs_noarg(char* arg_desc_dyn, char* context_optdyn);
static ClCPrsResult cl_prs_invarg(char* arg_dyn, char* error_dyn, char* context_optdyn);
static void cl_cmd_parse_res_free(ClCPrsResult* res);
static char* cl_cmd_get_str_dyn(struct InputConsoleData const* d_cl);
static char const* cl_cmd_from_enum(enum ClCTag t);
static char const* cl_set_prop_from_enum(enum ClCDSTag t);
static char const* cl_save_type_from_enum(enum ClCDSv t);
static enum ImageType cl_save_type_to_image_type(enum ClCDSv t);
// returns number of completions
static usize cl_compls_new(struct InputConsoleData* cl);
static void cl_free(struct InputConsoleData* cl);
static void cl_compls_free(struct InputConsoleData* cl);
static void cl_push(struct InputConsoleData* cl, char c);
static void cl_pop(struct InputConsoleData* cl);

static void input_mode_set(struct Ctx* ctx, enum InputTag mode_tag);
static void input_mode_free(struct InputMode* input_mode);
static char const* input_mode_as_str(enum InputTag mode_tag);
static void input_free(struct Input* input);

static void sel_circ_init_and_show(struct Ctx* ctx, i32 x, i32 y);
static void sel_circ_free_and_hide(struct SelectionCircle* sel_circ);
static i32 sel_circ_curr_item(struct SelectionCircle const* sc, i32 x, i32 y);

// separate functions, because they are callbacks
static Rect tool_selection_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event);
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
static void history_forward(struct Ctx* ctx, struct HistItem hist_opt);
static void history_apply(struct Ctx* ctx, struct HistItem* hist);
static void history_free(struct HistItem* hist);
static void historyarr_clear(struct HistItem** hist);

static Bool ximage_put_checked(XImage* im, u32 x, u32 y, argb col);
static Rect ximage_flood_fill(XImage* im, argb targ_col, i32 x, i32 y);
static Rect canvas_fill_rect(XImage* im, Pair c, Pair dims, argb col);
static Rect canvas_figure(struct Ctx* ctx, XImage* im, Pair p1, Pair p2);
// line from `a` to `b` is a polygon height (a is a base);
static Rect canvas_regular_poly(XImage* im, struct ToolCtx* tc, u32 n, Pair a, Pair b);
static Rect canvas_circle(XImage* im, struct ToolCtx* tc, circle_get_alpha_fn get_a, u32 d, Pair c);
static Rect canvas_line(XImage* im, struct ToolCtx* tc, enum DrawerShape shape, Pair from, Pair to, Bool draw_first_pt);
static Rect canvas_apply_drawer(XImage* im, struct ToolCtx* tc, enum DrawerShape shape, Pair c);
static Rect canvas_copy_region(XImage* dest, XImage* src, Pair from, Pair dims, Pair to);
static void canvas_fill(XImage* im, argb col);
static Bool canvas_load(struct Ctx* ctx, struct Image const* image);
static void canvas_free(struct Canvas* cv);
static void canvas_change_zoom(struct DrawCtx* dc, Pair cursor, i32 delta);
static void canvas_resize(struct Ctx* ctx, i32 new_width, i32 new_height);
static void overlay_clear(XImage* im);
static void overlay_dump(XImage* dest, XImage* overlay);
static void canvas_scroll(struct Canvas* cv, Pair delta);

static u32 statusline_height(struct DrawCtx const* dc);
// window size - interface parts (e.g. statusline)
static Pair clientarea_size(struct DrawCtx const* dc);
static Pair canvas_size(struct DrawCtx const* dc);
static void draw_string(struct DrawCtx* dc, char const* str, Pair c, enum Schm sc, Bool invert);
static void draw_int(struct DrawCtx* dc, i32 i, Pair c, enum Schm sc, Bool invert);
static int fill_rect(struct DrawCtx* dc, Pair p, Pair dim, argb col);
static int draw_line(struct DrawCtx* dc, Pair from, Pair to, enum Schm sc, Bool invert);
static u32 get_int_width(struct DrawCtx const* dc, char const* format, u32 i);
static u32 get_string_width(struct DrawCtx const* dc, char const* str, u32 len);
static void draw_selection_circle(struct DrawCtx* dc, struct SelectionCircle const* sc, i32 pointer_x, i32 pointer_y);
static void update_screen(struct Ctx* ctx);
static void update_statusline(struct Ctx* ctx);
static void show_message(struct Ctx* ctx, char const* msg);

static void dc_cache_init(struct DrawCtx* dc);
static void dc_cache_update_pm(struct DrawCtx* dc, Pixmap pm, XImage* im);
static void dc_cache_free(struct DrawCtx* dc);

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

void main_die_if_no_val_for_arg(
    char const* cmd_name,
    i32 argc,
    char** argv,
    u32 pos
) {
    if (pos + 1 == argc || argv[pos + 1][0] == '-') {
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
        } else if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--width")) {
            main_die_if_no_val_for_arg("-w or --width", argc, argv, i);
            // ctx.dc.width == ctx.dc.cv.im->width at program start
            ctx->dc.width = strtol(argv[++i], NULL, 0);
            if (!ctx->dc.width) {
                die("canvas width must be positive number");
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--height")) {
            main_die_if_no_val_for_arg("-h or --height", argc, argv, i);
            // ctx.dc.height == ctx.dc.cv.im->height at program start
            ctx->dc.height = strtol(argv[++i], NULL, 0);
            if (!ctx->dc.height) {
                die("canvas height must be positive number");
            }
        } else if (!strcmp(argv[i], "--help")) {
            main_show_help_message(stdout);
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
            result = False;
        }
    }

    return result;
}

void main_show_help_message(FILE* out) {
    fprintf(
        out,
        "Usage: xpaint [OPTIONS] [FILE]\n"
        "\n"
        "Options:\n"
        "      --help                   Print help message\n"
        "  -V, --version                Print version\n"
        "  -v, --verbose                Use verbose output\n"
        "  -w, --width <canvas width>   Set canvas width\n"
        "  -h, --height <canvas height> Set canvas height\n"
        "  -i, --input <file path>      Set load file\n"
        "  -o, --output <file path>     Set save file\n"
    );
}

void die(char const* errstr, ...) {
    va_list ap;

    fprintf(stderr, "xpaint: ");
    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

void trace(char const* fmt, ...) {
    if (is_verbose_output) {
        va_list ap;

        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        fprintf(stdout, "\n");
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

    char cp = arrpop(strarr);
    while (arrlen(strarr) && ((cp & 0x80) && !(cp & 0x40))) {
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
        case I_FigRect: return (D) {pic_fig_rect_data, RES_SZ_FIGURE_RECTANGLE};
        case I_FigCirc: return (D) {pic_fig_circ_data, RES_SZ_FIGURE_CIRCLE};
        case I_FigTri: return (D) {pic_fig_tri_data, RES_SZ_FIGURE_TRIANGLE};
        case I_FigFillOff:
            return (D) {pic_fig_fill_off_data, RES_SZ_FIGURE_FILL_OFF};
        case I_FigFillOn:
            return (D) {pic_fig_fill_on_data, RES_SZ_FIGURE_FILL_ON};
        default: return (D) {pic_unknown_data, RES_SZ_UNKNOWN};
    }
}

double brush_ease(double v) {
    return (v == 1.0) ? v : 1 - pow(2, -10 * v);
}

Bool state_match(u32 a, u32 b) {
// remove button masks (Button1Mask) and ignored masks
#define CLEANMASK(p_mask) \
    ((p_mask) \
     & (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask \
        | Mod5Mask) \
     & ~IGNOREMOD)

    return (a == ANY_MOD || b == ANY_MOD || CLEANMASK(a) == CLEANMASK(b));

#undef CLEANMASK
}

Button get_btn(XButtonEvent const* e) {
    return (Button) {e->button, e->state};
}

Bool btn_eq(Button a, Button b) {
    return state_match(a.mask, b.mask)
        && (a.button == ANY_KEY || b.button == ANY_KEY || a.button == b.button);
}

Bool key_eq(Key a, Key b) {
    return state_match(a.mask, b.mask)
        && (a.sym == ANY_KEY || b.sym == ANY_KEY || a.sym == b.sym);
}

static Bool can_action(struct Input const* input, Key curr_key, Action act) {
    if (!key_eq(curr_key, act.key)) {
        return False;
    }
    switch (input->mode.t) {
        case InputT_Interact: return (i32)act.mode & MF_Int;
        case InputT_Color: return (i32)act.mode & MF_Color;
        case InputT_Console: return False;  // managed manually
        case InputT_Transform: return (i32)act.mode & MF_Trans;
    }
    UNREACHABLE();
}

usize ximage_data_len(XImage const* im) {
    // FIXME is it correct?
    return (usize)im->width * im->height * (im->depth / 8);
}

void ximage_apply_trans(XImage** im_out, Transform t) {
    XImage* result =
        XSubImage(*im_out, 0, 0, (*im_out)->width, (*im_out)->height);

    overlay_clear(result);
    for (i32 i = 0; i < (*im_out)->width; ++i) {
        for (i32 j = 0; j < (*im_out)->height; ++j) {
            argb const pixel = XGetPixel(*im_out, i, j);
            ximage_put_checked(result, i + t.move.x, j + t.move.y, pixel);
        }
    }

    XDestroyImage(*im_out);
    *im_out = result;
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

Rect rect_expand(Rect a, Rect b) {
    return (Rect) {
        .l = MIN(a.l, b.l),
        .t = MIN(a.t, b.t),
        .r = MAX(a.r, b.r),
        .b = MAX(a.b, b.b),
    };
}

XTransform xtrans_scale(double z) {
    return (XTransform
    ) {{{XDoubleToFixed(z), XDoubleToFixed(0), XDoubleToFixed(0)},
        {XDoubleToFixed(0), XDoubleToFixed(z), XDoubleToFixed(0)},
        {XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1)}}};
}

XTransform xtrans_from_trans(Transform trans) {
    return (XTransform) {{
        {XDoubleToFixed(1), XDoubleToFixed(0), XDoubleToFixed(trans.move.x)},
        {XDoubleToFixed(0), XDoubleToFixed(1), XDoubleToFixed(trans.move.y)},
        {XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1)},
    }};
}

XTransform xtrans_mult(XTransform a, XTransform b) {
    XTransform result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.matrix[i][j] = XDoubleToFixed(0);
            for (int k = 0; k < 3; k++) {
                result.matrix[i][j] += XDoubleToFixed(
                    XFixedToDouble(a.matrix[i][k])
                    * XFixedToDouble(b.matrix[k][j])
                );
            }
        }
    }
    return result;
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
    vsnprintf(result, len + 1, fmt, ap2);
    va_end(ap2);
    return result;
}

void str_free(char** str_dyn) {
    if (*str_dyn) {
        free(*str_dyn);
        *str_dyn = NULL;
    }
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

void tc_free(struct ToolCtx* tc) {
    arrfree(tc->colarr);
}

Bool fnt_set(struct DrawCtx* dc, char const* font_name) {
    XftFont* xfont = XftFontOpenName(dc->dp, DefaultScreen(dc->dp), font_name);
    if (!xfont) {
        // FIXME never go there
        return False;
    }
    fnt_free(dc->dp, &dc->fnt);
    dc->fnt.xfont = xfont;
    dc->fnt.h = xfont->ascent + xfont->descent;
    return True;
}

void fnt_free(Display* dp, struct Fnt* fnt) {
    if (fnt->xfont) {
        XftFontClose(dp, fnt->xfont);
        fnt->xfont = NULL;
    }
}

struct IOCtx ioctx_new(char const* input) {
    if (!strcmp(input, IOCTX_STDIO_STR)) {
        return (struct IOCtx) {.t = IO_Stdio};
    }
    return (struct IOCtx) {
        .t = IO_File,
        .d.file.path_dyn = str_new("%s", input),
    };
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
}

Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p) {
    return point_from_cv_to_scr_xy(dc, p.x, p.y);
}

Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = (i32)(x * ZOOM_C(dc) + dc->cv.scroll.x),
        .y = (i32)(y * ZOOM_C(dc) + dc->cv.scroll.y),
    };
}

Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = (i32)((x - dc->cv.scroll.x) / ZOOM_C(dc)),
        .y = (i32)((y - dc->cv.scroll.y) / ZOOM_C(dc)),
    };
}

Bool point_in_rect(Pair p, Pair a1, Pair a2) {
    return MIN(a1.x, a2.x) < p.x && p.x < MAX(a1.x, a2.x)
        && MIN(a1.y, a2.y) < p.y && p.y < MAX(a1.y, a2.y);
}

Pair point_apply_trans(Pair p, Transform trans) {
    XFixed(*m)[3] = xtrans_from_trans(trans).matrix;

    // m[2] is not used
    return (Pair) {
        XFixedToDouble(m[0][0]) * p.x + XFixedToDouble(m[0][1]) * p.y
            + XFixedToDouble(m[0][2]) * 1,
        XFixedToDouble(m[1][0]) * p.x + XFixedToDouble(m[1][1]) * p.y
            + XFixedToDouble(m[1][2]) * 1,
    };
}

Pair dpt_to_pt(DPt p) {
    return (Pair) {.x = (i32)p.x, .y = (i32)p.y};
}

DPt dpt_rotate(DPt p, double deg) {
    double rad = deg * PI / 180.0;
    return (DPt) {
        .x = cos(rad) * p.x - sin(rad) * p.y,
        .y = sin(rad) * p.x + cos(rad) * p.y,
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
    if (len >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E
        && data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A
        && data[6] == 0x1A && data[7] == 0x0A) {
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
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
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

static u32 argb_to_abgr(argb v) {
    u32 const a = v & ARGB_ALPHA;
    u8 const red = (v & 0x00FF0000) >> (2 * 8);
    u32 const g = v & 0x0000FF00;
    u8 const blue = v & 0x000000FF;
    return a | blue << (2 * 8) | g | red;
}

static struct Image read_file_from_memory(
    struct DrawCtx const* dc,
    u8 const* data,
    u32 len,
    argb bg
) {
    i32 width = NIL;
    i32 height = NIL;
    i32 comp = NIL;
    stbi_uc* image_data =
        stbi_load_from_memory(data, (i32)len, &width, &height, &comp, 4);
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

struct Image
read_image_io(struct DrawCtx const* dc, struct IOCtx const* ioctx, argb bg) {
    switch (ioctx->t) {
        case IO_None: return (struct Image) {0};
        case IO_File: {
            int fd = open(ioctx->d.file.path_dyn, O_RDONLY);
            off_t len = lseek(fd, 0, SEEK_END);
            void* data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
            struct Image result = read_file_from_memory(dc, data, len, bg);

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
            FILE* fd = fopen(ctx->ioctx->d.file.path_dyn, "a");
            if (!fd) {
                return;
            }
            fwrite(data, sizeof(char), size, fd);
            fclose(fd);
        } break;
        case IO_Stdio: fwrite(data, sizeof(char), size, stdout); break;
    }

    ctx->result_out = True;
}

Bool write_io(
    struct DrawCtx* dc,
    struct Input const* input,
    enum ImageType type,
    struct IOCtx const* ioctx
) {
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
        remove(ioctx->d.file.path_dyn);
    }

    switch (type) {
        case IMT_Png: {
            stbi_write_png_compression_level = input->png_compression_level;
            struct IOCtxWriteCtx ioctx_write_ctx = {.ioctx = ioctx};
            result = stbi_write_png_to_func(
                &ioctx_write_part,
                (void*)&ioctx_write_ctx,
                w,
                h,
                4,
                rgba_dyn,
                0
            );
            result &= ioctx_write_ctx.result_out;
        } break;
        case IMT_Jpg: {
            i32 quality = input->jpg_quality_level;
            struct IOCtxWriteCtx ioctx_write_ctx = {.ioctx = ioctx};
            result = stbi_write_jpg_to_func(
                &ioctx_write_part,
                (void*)&ioctx_write_ctx,
                w,
                h,
                4,
                rgba_dyn,
                quality
            );
            result &= ioctx_write_ctx.result_out;
        } break;
        case IMT_Unknown: UNREACHABLE();
    }
    free(rgba_dyn);
    return result;
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
                case ClCDS_Font: {
                    char const* font = cl_cmd->d.set.d.font.name_dyn;
                    if (!fnt_set(&ctx->dc, font)) {
                        msg_to_show = str_new("invalid font name: '%s'", font);
                    }
                } break;
                case ClCDS_Inp: {
                    char const* path = cl_cmd->d.set.d.inp.path_dyn;
                    ioctx_set(&ctx->inp, path);
                    msg_to_show =
                        str_new("inp set to '%s'", ioctx_as_str(&ctx->inp));
                } break;
                case ClCDS_Out: {
                    char const* path = cl_cmd->d.set.d.out.path_dyn;
                    ioctx_set(&ctx->out, path);
                    msg_to_show =
                        str_new("out set to '%s'", ioctx_as_str(&ctx->out));
                } break;
                case ClCDS_PngCompression: {
                    ctx->input.png_compression_level =
                        cl_cmd->d.set.d.png_cpr.compression;
                } break;
                case ClCDS_JpgQuality: {
                    ctx->input.jpg_quality_level =
                        cl_cmd->d.set.d.jpg_qlt.quality;
                } break;
                case ClCDS_Spacing: {
                    if (TC_IS_DRAWER(&CURR_TC(ctx))) {
                        if (cl_cmd->d.set.d.spacing.val >= 1) {
                            CURR_TC(ctx).d.drawer.spacing =
                                cl_cmd->d.set.d.spacing.val;
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
        case ClC_Save: {
            if (!cl_cmd->d.save.path_dyn && ctx->out.t == IO_None) {
                msg_to_show = str_new("can't save: no path provided");
            } else {
                struct IOCtx const ioctx = cl_cmd->d.save.path_dyn
                    ? ioctx_new(cl_cmd->d.save.path_dyn)
                    : ctx->out;
                msg_to_show = str_new(
                    write_io(
                        &ctx->dc,
                        &ctx->input,
                        cl_save_type_to_image_type(cl_cmd->d.save.im_type),
                        &ioctx
                    )
                        ? "image saved to '%s'"
                        : "failed save image to '%s'",
                    ioctx_as_str(&ioctx)
                );
            }
        } break;
        case ClC_Load: {
            struct IOCtx const ioctx = cl_cmd->d.load.path_dyn
                ? ioctx_new(cl_cmd->d.load.path_dyn)
                : ctx->inp;
            struct Image im = read_image_io(&ctx->dc, &ioctx, 0);

            XImage* old_cv = ctx->dc.cv.im;
            Rect old_cv_rect = (Rect) {0, 0, old_cv->width, old_cv->height};
            struct HistItem to_push = history_new_item(old_cv, old_cv_rect);

            if (canvas_load(ctx, &im)) {
                history_forward(ctx, to_push);
                msg_to_show =
                    str_new("image_loaded from '%s'", ioctx_as_str(&ioctx));
            } else {
                history_free(&to_push);
                msg_to_show = str_new(
                    "failed load image from '%s'",
                    ioctx_as_str(&ioctx)
                );
            }
        } break;
        case ClC_Last: assert(!"invalid enum value");
    }
    bit_status |= msg_to_show ? ClCPrc_Msg : 0;
    return (ClCPrcResult) {.bit_status = bit_status, .msg_dyn = msg_to_show};
}

static ClCPrsResult cl_cmd_parse_helper(struct Ctx* ctx, char* cl) {
    char const* cmd = strtok(cl, CL_DELIM);
    if (!cmd) {
        return cl_prs_noarg(str_new("command"), NULL);
    }
    if (!strcmp(cmd, "echo")) {
        char const* user_msg = strtok(NULL, "");
        return (ClCPrsResult
        ) {.t = ClCPrs_Ok,
           .d.ok.t = ClC_Echo,
           .d.ok.d.echo.msg_dyn = str_new("%s", COALESCE(user_msg, ""))};
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
                return cl_prs_invarg(
                    str_new("%s", arg),
                    str_new("value must be in [%d .. %d]", MIN_LW, MAX_LW),
                    NULL
                );
            }
            return (ClCPrsResult) {
                .t = ClCPrs_Ok,
                .d.ok.t = ClC_Set,
                .d.ok.d.set.t = ClCDS_LineW,
                .d.ok.d.set.d.line_w.value = line_w,
            };
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Col))) {
            char const* arg = strtok(NULL, CL_DELIM);
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_Col,
               .d.ok.d.set.d.col.v =
                   arg ? (strtol(arg, NULL, 16) & 0xFFFFFF) | 0xFF000000 : 0};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Font))) {
            char const* font = strtok(NULL, CL_DELIM);
            if (!font) {
                return cl_prs_noarg(str_new("font"), NULL);
            }
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_Font,
               .d.ok.d.set.d.font.name_dyn = str_new("%s", font)};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Inp))) {
            char const* path = strtok(NULL, "");  // user can load NULL
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_Inp,
               .d.ok.d.set.d.inp.path_dyn = path ? str_new("%s", path) : NULL};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Out))) {
            char const* path = strtok(NULL, "");  // user can load NULL
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_Out,
               .d.ok.d.set.d.out.path_dyn = path ? str_new("%s", path) : NULL};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_PngCompression))) {
            char const* compression = strtok(NULL, CL_DELIM);
            if (!compression) {
                return cl_prs_noarg(str_new("compression value"), NULL);
            }
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_PngCompression,
               .d.ok.d.set.d.png_cpr.compression =
                   (i32)strtol(compression, NULL, 0)};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_JpgQuality))) {
            char const* quality = strtok(NULL, CL_DELIM);
            if (!quality) {
                return cl_prs_noarg(str_new("image quality"), NULL);
            }
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_JpgQuality,
               .d.ok.d.set.d.jpg_qlt.quality = (i32)strtol(quality, NULL, 0)};
        }
        if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Spacing))) {
            char const* spacing = strtok(NULL, CL_DELIM);
            if (!spacing) {
                return cl_prs_noarg(str_new("spacing"), NULL);
            }
            return (ClCPrsResult
            ) {.t = ClCPrs_Ok,
               .d.ok.t = ClC_Set,
               .d.ok.d.set.t = ClCDS_Spacing,
               .d.ok.d.set.d.spacing.val = strtoul(spacing, NULL, 0)};
        }
        return cl_prs_invarg(
            str_new("%s", prop),
            str_new("unknown prop"),
            str_new("%s", cl_cmd_from_enum(ClC_Set))
        );
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_Exit))) {
        return (ClCPrsResult) {.t = ClCPrs_Ok, .d.ok.t = ClC_Exit};
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
            return cl_prs_invarg(
                str_new("%s", type_str),
                str_new("unknown type"),
                NULL
            );
        }
        char const* path = strtok(NULL, "");  // include spaces
        return (ClCPrsResult
        ) {.t = ClCPrs_Ok,
           .d.ok.t = ClC_Save,
           .d.ok.d.save.im_type = type,
           .d.ok.d.save.path_dyn = path ? str_new("%s", path) : NULL};
    }
    if (!strcmp(cmd, cl_cmd_from_enum(ClC_Load))) {
        char const* path = strtok(NULL, "");  // path with spaces
        return (ClCPrsResult
        ) {.t = ClCPrs_Ok,
           .d.ok.t = ClC_Load,
           .d.ok.d.load.path_dyn = str_new("%s", path)};
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
    ) {.t = ClCPrs_ENoArg,
       .d.noarg.arg_desc_dyn = arg_desc_dyn,
       .d.noarg.context_optdyn = context_optdyn};
}

ClCPrsResult
cl_prs_invarg(char* arg_dyn, char* error_dyn, char* context_optdyn) {
    return (ClCPrsResult
    ) {.t = ClCPrs_EInvArg,
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
                        case ClCDS_Font:
                            free(cl_cmd->d.set.d.font.name_dyn);
                            break;
                        case ClCDS_Inp:
                            free(cl_cmd->d.set.d.inp.path_dyn);
                            break;
                        case ClCDS_Out:
                            free(cl_cmd->d.set.d.out.path_dyn);
                            break;
                        case ClCDS_LineW:
                        case ClCDS_Col:
                        case ClCDS_PngCompression:
                        case ClCDS_JpgQuality:
                        case ClCDS_Spacing:
                        case ClCDS_Last:
                            break;  // no default branch to enable warnings
                    }
                    break;
                case ClC_Save: free(cl_cmd->d.save.path_dyn); break;
                case ClC_Load: free(cl_cmd->d.load.path_dyn); break;
                case ClC_Echo: free(cl_cmd->d.echo.msg_dyn); break;
                case ClC_Exit: break;  // no default branch to enable warnings
                case ClC_Last: assert(!"invalid enum value");
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
        case ClC_Set: return "set";
        case ClC_Last: return "last";
    }
    UNREACHABLE();
}

static char const* cl_set_prop_from_enum(enum ClCDSTag t) {
    switch (t) {
        case ClCDS_Col: return "col";
        case ClCDS_Inp: return "inp";
        case ClCDS_Out: return "out";
        case ClCDS_Font: return "font";
        case ClCDS_LineW: return "line_w";
        case ClCDS_PngCompression: return "png_cmpr";
        case ClCDS_JpgQuality: return "jpg_qlty";
        case ClCDS_Spacing: return "spacing";
        case ClCDS_Last: return "last";
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

static void cl_compls_update_helper(
    char*** result,
    char const* token,
    char const* (*enum_to_str)(usize),
    usize enum_last,
    Bool add_delim
) {
    if (!token || !enum_to_str || !result) {
        return;
    }
    for (u32 e = 0; e < enum_last; ++e) {
        char const* enum_str = enum_to_str(e);
        usize offset = first_dismatch(enum_str, token);
        if (offset == strlen(token)) {
            // don't let completions stick with commands
            char const* prefix =
                add_delim && strlen(token) == 0 ? CL_DELIM : "";
            char* complt = str_new("%s%s", prefix, enum_str + offset);
            arrpush(*result, complt);
        }
    }
}

usize cl_compls_new(struct InputConsoleData* cl) {
    char* cl_buf_dyn = cl_cmd_get_str_dyn(cl);
    char** result = NULL;

    usize const cl_buf_len = strlen(cl_buf_dyn);
    Bool const last_char_is_space =
        cl_buf_len != 0 && cl_buf_dyn[cl_buf_len - 1] == CL_DELIM[0];
    // prepend delim to completions if not provided
    Bool const add_delim = cl_buf_len != 0 && !last_char_is_space;

    char const* tok1 = strtok(cl_buf_dyn, CL_DELIM);
    char const* tok2 = strtok(NULL, "");
    tok1 = COALESCE(tok1, "");  // don't do strtok in macro
    tok2 = COALESCE(tok2, "");

    cl_compls_free(cl);

    typedef char const* (*cast)(usize);
    // subcommands with own completions
    if (!strcmp(tok1, cl_cmd_from_enum(ClC_Set))) {
        cl_compls_update_helper(
            &result,
            tok2,
            (cast)&cl_set_prop_from_enum,
            ClCDS_Last,
            add_delim
        );
    } else if (!strcmp(tok1, cl_cmd_from_enum(ClC_Save))) {
        cl_compls_update_helper(
            &result,
            tok2,
            (cast)&cl_save_type_from_enum,
            ClCDSv_Last,
            add_delim
        );
    } else if (strlen(tok1) == 0 || !last_char_is_space) {  // first token comletion
        cl_compls_update_helper(
            &result,
            tok1,
            (cast)&cl_cmd_from_enum,
            ClC_Last,
            add_delim
        );
    } else {
        free(cl_buf_dyn);
        return 0;
    }

    free(cl_buf_dyn);

    cl->compls_arr = result;
    if (cl->compls_arr) {
        cl->compls_valid = True;
    }

    return arrlen(cl->compls_arr);
}

void cl_free(struct InputConsoleData* cl) {
    arrfree(cl->cmdarr);
    cl_compls_free(cl);
}

void cl_compls_free(struct InputConsoleData* cl) {
    if (cl->compls_arr) {
        for (u32 i = 0; i < arrlen(cl->compls_arr); ++i) {
            str_free(&cl->compls_arr[i]);
        }
        arrfree(cl->compls_arr);
        cl->compls_arr = NULL;
        cl->compls_valid = False;
        cl->compls_curr = 0;
    }
}

void cl_push(struct InputConsoleData* cl, char c) {
    arrpush(cl->cmdarr, c);
    cl_compls_free(cl);
}

void cl_pop(struct InputConsoleData* cl) {
    if (!cl->cmdarr) {
        return;
    }
    usize const size = arrlen(cl->cmdarr);
    if (size) {
        arrpoputf8(cl->cmdarr);
    }
    cl_compls_free(cl);
}

void input_mode_set(struct Ctx* ctx, enum InputTag const mode_tag) {
    struct Input* inp = &ctx->input;

    switch (inp->mode.t) {
        case InputT_Transform:
            ximage_apply_trans(&inp->overlay, OVERLAY_TRANSFORM(&inp->mode));
            overlay_dump(ctx->dc.cv.im, inp->overlay);
            overlay_clear(inp->overlay);
            update_screen(ctx);
            break;
        default: break;
    }

    input_mode_free(&inp->mode);
    inp->mode.t = mode_tag;

    switch (inp->mode.t) {
        case InputT_Color:
            inp->mode.d.col = (struct InputColorData) {.current_digit = 0};
            break;
        case InputT_Console:
            inp->mode.d.cl = (struct InputConsoleData
            ) {.cmdarr = NULL, .compls_valid = False};
            break;
        case InputT_Interact: break;
        case InputT_Transform:
            inp->mode.d.trans =
                (struct InputTransformData) {.overlay_bounds = RNIL};
            break;
    }
}

void input_mode_free(struct InputMode* input_mode) {
    switch (input_mode->t) {
        case InputT_Console: cl_free(&input_mode->d.cl); break;
        default: break;
    }
}

char const* input_mode_as_str(enum InputTag mode_tag) {
    switch (mode_tag) {
        case InputT_Interact: return "INT";
        case InputT_Color: return "COL";
        case InputT_Console: return "CMD";
        case InputT_Transform: return "TFM";
    }
    return "???";
}

void input_free(struct Input* input) {
    input_mode_free(&input->mode);
    if (input->overlay) {
        XDestroyImage(input->overlay);
        input->overlay = NULL;
    }
}

// clang-format off
static void sel_circ_set_tool_selection(struct Ctx* ctx) { tc_set_tool(&CURR_TC(ctx), Tool_Selection); }
static void sel_circ_set_tool_pencil(struct Ctx* ctx) { tc_set_tool(&CURR_TC(ctx), Tool_Pencil); }
static void sel_circ_set_tool_fill(struct Ctx* ctx) { tc_set_tool(&CURR_TC(ctx), Tool_Fill); }
static void sel_circ_set_tool_picker(struct Ctx* ctx) { tc_set_tool(&CURR_TC(ctx), Tool_Picker); }
static void sel_circ_set_tool_brush(struct Ctx* ctx) { tc_set_tool(&CURR_TC(ctx), Tool_Brush); }
static void sel_circ_set_tool_figure(struct Ctx* ctx) { tc_set_tool(&CURR_TC(ctx), Tool_Figure); }
static void sel_circ_figure_toggle_fill(struct Ctx* ctx) { CURR_TC(ctx).d.fig.fill ^= 1; }
static void sel_circ_set_figure(struct Ctx* ctx, enum FigureType type) {
    struct ToolCtx* tc = &CURR_TC(ctx);
    assert(tc->t == Tool_Figure);
    tc->d.fig.curr = type;
}
static void sel_circ_figure_set_circle(struct Ctx* ctx) { sel_circ_set_figure(ctx, Figure_Circle); }
static void sel_circ_figure_set_rectangle(struct Ctx* ctx) { sel_circ_set_figure(ctx, Figure_Rectangle); }
static void sel_circ_figure_set_triangle(struct Ctx* ctx) { sel_circ_set_figure(ctx, Figure_Triangle); }
// clang-format on

void sel_circ_init_and_show(struct Ctx* ctx, i32 x, i32 y) {
    sel_circ_free_and_hide(&ctx->sc);

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct SelectionCircle* sc = &ctx->sc;

    if (tc->t == Tool_Figure) {
        struct Item items[] = {
            {.on_select = &sel_circ_figure_set_circle, .icon = I_FigCirc},
            {.on_select = &sel_circ_figure_set_rectangle, .icon = I_FigRect},
            {.on_select = &sel_circ_figure_set_triangle, .icon = I_FigTri},
            {.on_select = &sel_circ_figure_toggle_fill,
             .icon = tc->d.fig.fill ? I_FigFillOff : I_FigFillOn},
            {.on_select = &sel_circ_set_tool_pencil, .icon = I_Pencil},
        };
        for (u32 i = 0; i < LENGTH(items); ++i) {
            arrpush(sc->items_arr, items[i]);
        }
    } else {
        struct Item items[] = {
            {.on_select = &sel_circ_set_tool_selection, .icon = I_Select},
            {.on_select = &sel_circ_set_tool_pencil, .icon = I_Pencil},
            {.on_select = &sel_circ_set_tool_fill, .icon = I_Fill},
            {.on_select = &sel_circ_set_tool_picker, .icon = I_Picker},
            {.on_select = &sel_circ_set_tool_brush, .icon = I_Brush},
            {.on_select = &sel_circ_set_tool_figure, .icon = I_Figure},
        };
        for (u32 i = 0; i < LENGTH(items); ++i) {
            arrpush(sc->items_arr, items[i]);
        }
    }
    ctx->sc.x = x;
    ctx->sc.y = y;
    ctx->sc.is_active = True;
}

void sel_circ_free_and_hide(struct SelectionCircle* sel_circ) {
    if (sel_circ->items_arr) {
        arrfree(sel_circ->items_arr);
        sel_circ->items_arr = NULL;
    }
    sel_circ->is_active = False;
}

i32 sel_circ_curr_item(struct SelectionCircle const* sc, i32 x, i32 y) {
    i32 const pointer_x_rel = x - sc->x;
    i32 const pointer_y_rel = y - sc->y;
    if (pointer_x_rel == 0 && pointer_y_rel == 0) {
        return NIL;  // prevent 0.0 / 0.0 division
    }
    double const segment_rad = PI * 2 / MAX(1, arrlen(sc->items_arr));
    double const segment_deg = segment_rad / PI * 180;
    double const pointer_r =
        sqrt(pointer_x_rel * pointer_x_rel + pointer_y_rel * pointer_y_rel);

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

Rect tool_selection_on_release(
    struct Ctx* ctx,
    XButtonReleasedEvent const* event
) {
    if (!btn_eq(get_btn(event), BTN_MAIN)
        && !btn_eq(get_btn(event), BTN_COPY_SELECTION)) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;

    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    i32 const begin_x = CLAMP(inp->press_pt.x, 0, dc->cv.im->width);
    i32 const begin_y = CLAMP(inp->press_pt.y, 0, dc->cv.im->height);
    i32 const end_x = CLAMP(pointer.x, 0, dc->cv.im->width);
    i32 const end_y = CLAMP(pointer.y, 0, dc->cv.im->height);

    Pair p = {MIN(begin_x, end_x), MIN(begin_y, end_y)};
    Pair dims = (Pair) {MAX(begin_x, end_x) - p.x, MAX(begin_y, end_y) - p.y};
    overlay_clear(inp->overlay);
    Rect damage = canvas_copy_region(inp->overlay, dc->cv.im, p, dims, p);

    // move on BTN_MAIN, copy on BTN_COPY_SELECTION
    if (!btn_eq(get_btn(event), BTN_COPY_SELECTION)) {
        argb bg_col = CANVAS_BACKGROUND;  // FIXME set in runtime?
        canvas_fill_rect(dc->cv.im, p, dims, bg_col);
    }

    input_mode_set(ctx, InputT_Transform);

    return damage;
}

Rect tool_selection_on_drag(struct Ctx* ctx, XMotionEvent const* event) {
    if (!btn_eq(ctx->input.holding_button, BTN_MAIN)
        && !btn_eq(ctx->input.holding_button, BTN_COPY_SELECTION)) {
        return RNIL;
    }

    struct Input* inp = &ctx->input;
    struct DrawCtx* dc = &ctx->dc;
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    i32 const begin_x = MIN(pointer.x, inp->press_pt.x);
    i32 const begin_y = MIN(pointer.y, inp->press_pt.y);
    i32 const end_x = MAX(pointer.x, inp->press_pt.x);
    i32 const end_y = MAX(pointer.y, inp->press_pt.y);
    i32 const w = end_x - begin_x;
    i32 const h = end_y - begin_y;

    overlay_clear(inp->overlay);
    return canvas_fill_rect(
        inp->overlay,
        (Pair) {begin_x, begin_y},
        (Pair) {w, h},
        SEL_TOOL_COL
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
            ctx->input.overlay,
            tc,
            tc->d.drawer.shape,
            point_from_scr_to_cv_xy(dc, event->x, event->y)
        );
    }

    return RNIL;
}

Rect tool_drawer_on_release(
    struct Ctx* ctx,
    XButtonReleasedEvent const* event
) {
    if (!btn_eq(get_btn(event), BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct DrawerData* drawer = &tc->d.drawer;

    Pair begin = ctx->input.anchor;
    Pair end = point_from_scr_to_cv_xy(dc, event->x, event->y);
    XImage* const im = ctx->input.overlay;

    ctx->input.anchor = end;

    // all points with drag motion drawn in on_drag
    if (!ctx->input.is_dragging && state_match(event->state, ShiftMask)) {
        return canvas_line(im, tc, drawer->shape, begin, end, False);
    }

    return RNIL;
}

Rect tool_drawer_on_drag(struct Ctx* ctx, XMotionEvent const* event) {
    if (!btn_eq(ctx->input.holding_button, BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct DrawerData const* drawer = &tc->d.drawer;
    XImage* const im = ctx->input.overlay;

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

Rect tool_figure_on_release(
    struct Ctx* ctx,
    XButtonReleasedEvent const* event
) {
    if (!btn_eq(get_btn(event), BTN_MAIN)
        || (!ctx->input.is_dragging && !(state_match(event->state, ShiftMask))
        )) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair anchor = ctx->input.anchor;

    overlay_clear(ctx->input.overlay);
    return canvas_figure(ctx, ctx->input.overlay, pointer, anchor);
}

Rect tool_figure_on_drag(struct Ctx* ctx, XMotionEvent const* event) {
    if (!btn_eq(ctx->input.holding_button, BTN_MAIN)) {
        return RNIL;
    }

    struct DrawCtx* dc = &ctx->dc;
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair anchor = ctx->input.anchor;

    overlay_clear(ctx->input.overlay);
    ctx->input.damage = RNIL;  // also undo damage

    return canvas_figure(ctx, ctx->input.overlay, pointer, anchor);
}

Rect tool_fill_on_release(struct Ctx* ctx, XButtonReleasedEvent const* event) {
    if (!btn_eq(ctx->input.holding_button, BTN_MAIN)) {
        return RNIL;
    }

    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    struct Input* inp = &ctx->input;

    // copy whole canvas to overlay (flood_fill must know surround pixels)
    usize data_len = ximage_data_len(dc->cv.im);
    // XXX is XImage::data public member?
    memcpy(inp->overlay->data, dc->cv.im->data, data_len);

    Pair const cur = point_from_scr_to_cv_xy(dc, event->x, event->y);

    return ximage_flood_fill(inp->overlay, *tc_curr_col(tc), cur.x, cur.y);
}

Rect tool_picker_on_release(
    struct Ctx* ctx,
    XButtonReleasedEvent const* event
) {
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct DrawCtx* dc = &ctx->dc;
    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    XImage* im = dc->cv.im;  // overlay always empty, grab from canvas

    if (!point_in_rect(
            pointer,
            (Pair) {0, 0},
            (Pair) {(i32)im->width, (i32)im->height}
        )) {
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
    struct HistItem** hist_pop =
        forward ? &ctx->hist_nextarr : &ctx->hist_prevarr;
    struct HistItem** hist_save =
        forward ? &ctx->hist_prevarr : &ctx->hist_nextarr;

    if (!arrlenu(*hist_pop)) {
        return False;
    }

    struct HistItem curr = arrpop(*hist_pop);
    Rect curr_rect = (Rect
    ) {curr.pivot.x,
       curr.pivot.y,
       curr.pivot.x + curr.patch->width,
       curr.pivot.y + curr.patch->height};

    arrpush(*hist_save, history_new_item(ctx->dc.cv.im, curr_rect));
    history_apply(ctx, &curr);
    history_free(&curr);

    return True;
}

void history_forward(struct Ctx* ctx, struct HistItem hist_opt) {
    trace("xpaint: history forward");
    // next history invalidated after user action
    historyarr_clear(&ctx->hist_nextarr);
    arrpush(ctx->hist_prevarr, hist_opt);
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

Bool ximage_put_checked(XImage* im, u32 x, u32 y, argb col) {
    if (x >= im->width || y >= im->height) {
        return False;
    }

    XPutPixel(im, x, y, col);
    return True;
}

Rect ximage_flood_fill(XImage* im, argb targ_col, i32 x, i32 y) {
    assert(im);
    if (x < 0 || y < 0 || x >= im->width || y >= im->height) {
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

            if (d_curr.x < 0 || d_curr.y < 0 || d_curr.x >= im->width
                || d_curr.y >= im->height) {
                continue;
            }

            if (XGetPixel(im, d_curr.x, d_curr.y) == area_col) {
                XPutPixel(im, d_curr.x, d_curr.y, targ_col);
                damage = rect_expand(
                    damage,
                    (Rect) {d_curr.x, d_curr.y, d_curr.x, d_curr.y}
                );

                arrpush(queue_arr, d_curr);
            }
        }
    }

    arrfree(queue_arr);

    return damage;
}

Rect canvas_fill_rect(XImage* im, Pair c, Pair dims, argb col) {
    Rect damage = RNIL;

    Bool const nx = dims.x < 0;
    Bool const ny = dims.y < 0;
    for (i32 x = c.x + (nx ? dims.x : 0); x < c.x + (nx ? 0 : dims.x); ++x) {
        for (i32 y = c.y + (ny ? dims.y : 0); y < c.y + (ny ? 0 : dims.y);
             ++y) {
            if (ximage_put_checked(im, x, y, col)) {
                damage = rect_expand(damage, (Rect) {x, y, x, y});
            }
        }
    }

    return damage;
}

// FIXME w unused (required because of circle_get_alpha_fn)
static u8 canvas_brush_get_a(u32 w, double r, Pair p) {
    double const curr_r = sqrt((p.x - r) * (p.x - r) + (p.y - r) * (p.y - r));
    return (u32)((1.0 - brush_ease(curr_r / r)) * 0xFF);
}

static u8 canvas_figure_circle_get_a_fill(u32 w, double r, Pair p) {
    return 0xFF;
}

static u8 canvas_figure_circle_get_a(u32 w, double r, Pair p) {
    double const curr_r = sqrt((p.x - r) * (p.x - r) + (p.y - r) * (p.y - r));
    return (r - curr_r < w) * 0xFF;
}

static Pair
get_fig_fill_pt(enum FigureType type, Pair a, Pair b, Pair im_dims) {
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
    i32 const dx = p1.x - p2.x;
    i32 const dy = p1.y - p2.y;

    switch (fig->curr) {
        case Figure_Circle: {
            double const d = sqrt(dx * dx + dy * dy);
            return canvas_circle(
                im,
                tc,
                fig->fill ? &canvas_figure_circle_get_a_fill
                          : &canvas_figure_circle_get_a,
                (u32)d,
                (Pair) {(p1.x + p2.x) / 2, (p1.y + p2.y) / 2}
            );
        }
        case Figure_Rectangle:
        case Figure_Triangle: {
            u32 sides = fig->curr == Figure_Triangle ? 3 : 4;
            Rect damage = canvas_regular_poly(im, tc, sides, p2, p1);
            if (fig->fill) {
                Pair const im_dims = {im->width, im->height};
                Pair const fill_pt =
                    get_fig_fill_pt(fig->curr, p2, p1, im_dims);
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

Rect canvas_regular_poly(
    XImage* im,
    struct ToolCtx* tc,
    u32 n,
    Pair a,
    Pair b
) {
    Rect damage = RNIL;
    DPt const inr_circmr = get_inr_circmr_cfs(n);
    DPt c = {
        a.x + (b.x - a.x) * inr_circmr.x,
        a.y + (b.y - a.y) * inr_circmr.x,
    };
    DPt curr = {
        (b.x - a.x) * inr_circmr.y,
        (b.y - a.y) * inr_circmr.y,
    };
    DPt prev = curr;
    for (u32 i = 0; i < n; ++i) {
        curr = dpt_rotate(curr, 360.0 / n);
        Rect line_damage = canvas_line(
            im,
            tc,
            DS_Square,
            dpt_to_pt(dpt_add(c, prev)),
            dpt_to_pt(dpt_add(c, curr)),
            True
        );
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

Rect canvas_apply_drawer(
    XImage* im,
    struct ToolCtx* tc,
    enum DrawerShape shape,
    Pair c
) {
    u32 const w = tc->line_w;
    switch (shape) {
        case DS_Circle: {
            u32 stroke_d = (u32)(w * BRUSH_LINE_W_MOD);
            return canvas_circle(im, tc, &canvas_brush_get_a, stroke_d, c);
        }
        case DS_Square: {
            Pair c2 = (Pair) {c.x - (i32)w / 2, c.y - (i32)w / 2};
            Pair dims = (Pair) {(i32)w, (i32)w};
            return canvas_fill_rect(im, c2, dims, *tc_curr_col(tc));
        }
    }
    return RNIL;
}

Rect canvas_circle(
    XImage* im,
    struct ToolCtx* tc,
    circle_get_alpha_fn get_a,
    u32 d,
    Pair c
) {
    Rect damage = RNIL;
    argb const col = *tc_curr_col(tc);
    u32 const w = tc->line_w;

    if (d == 1) {
        ximage_put_checked(im, c.x, c.y, col);
        return RNIL;
    }
    double const r = d / 2.0;
    double const r_sq = r * r;
    u32 const l = c.x - (u32)r;
    u32 const t = c.y - (u32)r;
    for (i32 dx = 0; dx < d; ++dx) {
        for (i32 dy = 0; dy < d; ++dy) {
            double const dr = (dx - r) * (dx - r) + (dy - r) * (dy - r);
            u32 const x = l + dx;
            u32 const y = t + dy;
            if (!BETWEEN(x, 0, im->width - 1) || !BETWEEN(y, 0, im->height - 1)
                || dr > r_sq) {
                continue;
            }
            argb const bg = XGetPixel(im, x, y);
            argb const blended =
                argb_blend(col, bg, get_a(w, r, (Pair) {dx, dy}));
            XPutPixel(im, x, y, blended);
            damage =
                rect_expand(damage, (Rect) {(i32)x, (i32)y, (i32)x, (i32)y});
        }
    }

    return damage;
}

Rect canvas_copy_region(
    XImage* dest,
    XImage* src,
    Pair from,
    Pair dims,
    Pair to
) {
    i32 const w = src->width;
    i32 const h = src->height;
    assert(from.x >= 0 && from.y >= 0);
    assert(from.x + dims.x <= w && from.y + dims.y <= h);

    // FIXME alloc only dims.x * dims.y
    u32* region_dyn = (u32*)ecalloc(w * h, sizeof(u32));
    for (i32 get_or_set = 1; get_or_set >= 0; --get_or_set) {
        for (i32 y = 0; y < dims.y; ++y) {
            for (i32 x = 0; x < dims.x; ++x) {
                if (get_or_set) {
                    region_dyn[y * w + x] =
                        XGetPixel(src, from.x + x, from.y + y);
                } else {
                    ximage_put_checked(
                        dest,
                        to.x + x,
                        to.y + y,
                        region_dyn[y * w + x]
                    );
                }
            }
        }
    }

    free(region_dyn);
    return (Rect) {
        MAX(0, to.x),
        MAX(0, to.y),
        MIN(dest->width - 1, to.x + dims.x),
        MIN(dest->height - 1, to.y + dims.y),
    };
}

void canvas_fill(XImage* im, argb col) {
    for (i32 i = 0; i < im->width; ++i) {
        for (i32 j = 0; j < im->height; ++j) {
            XPutPixel(im, i, j, col);
        }
    }
}

static Bool canvas_load(struct Ctx* ctx, struct Image const* image) {
    if (!image->im) {
        return False;
    }
    struct DrawCtx* dc = &ctx->dc;

    canvas_free(&dc->cv);
    dc->cv.im = image->im;
    dc->cv.type = image->type;
    ctx->input.overlay =
        XSubImage(dc->cv.im, 0, 0, dc->cv.im->width, dc->cv.im->height);
    overlay_clear(ctx->input.overlay);
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

void canvas_resize(struct Ctx* ctx, i32 new_width, i32 new_height) {
    if (new_width <= 0 || new_height <= 0) {
        trace("resize_canvas: invalid canvas size");
        return;
    }
    struct DrawCtx* dc = &ctx->dc;
    u32 const old_width = dc->cv.im->width;
    u32 const old_height = dc->cv.im->height;

    // resize overlay too
    XImage* old_overlay = ctx->input.overlay;
    ctx->input.overlay =
        XSubImage(ctx->input.overlay, 0, 0, new_width, new_height);
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
            (Pair) {(i32)(new_width - old_width), new_height},
            CANVAS_BACKGROUND
        );
    }
    if (old_height < new_height) {
        canvas_fill_rect(
            dc->cv.im,
            (Pair) {0, (i32)old_height},
            (Pair) {new_width, (i32)(new_height - old_height)},
            CANVAS_BACKGROUND
        );
    }
}

void overlay_clear(XImage* im) {
    u32 const data_size = ximage_data_len(im);
    // XXX is data a public member?
    memset(im->data, 0x0, data_size);
}

void overlay_dump(XImage* dest, XImage* overlay) {
    for (i32 x = 0; x < overlay->width; ++x) {
        for (i32 y = 0; y < overlay->height; ++y) {
            argb ovr = XGetPixel(overlay, x, y);
            if (ovr & ARGB_ALPHA) {
                argb bg = XGetPixel(dest, x, y);
                // make first argument opaque and use its opacity as third argument.
                argb result =
                    argb_blend(argb_normalize(ovr), bg, (ovr >> 0x18) & 0xFF);
                ximage_put_checked(dest, x, y, result);
            }
        }
    }
}

void canvas_scroll(struct Canvas* cv, Pair delta) {
    cv->scroll.x += delta.x;
    cv->scroll.y += delta.y;
}

u32 statusline_height(struct DrawCtx const* dc) {
    return dc->fnt.xfont->ascent + STATUSLINE_PADDING_BOTTOM;
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

void draw_string(
    struct DrawCtx* dc,
    char const* str,
    Pair c,
    enum Schm sc,
    Bool invert
) {
    XftDraw* d = XftDrawCreate(
        dc->dp,
        dc->back_buffer,
        dc->sys.vinfo.visual,
        dc->sys.colmap
    );
    XftDrawStringUtf8(
        d,
        invert ? &dc->schemes_dyn[sc].bg : &dc->schemes_dyn[sc].fg,
        dc->fnt.xfont,
        c.x,
        c.y,
        (XftChar8*)str,
        (i32)strlen(str)
    );
    XftDrawDestroy(d);
}

void draw_int(struct DrawCtx* dc, i32 i, Pair c, enum Schm sc, Bool invert) {
    char* msg = str_new("%d", i);
    draw_string(dc, msg, c, sc, invert);
    str_free(&msg);
}

// XXX always opaque
int fill_rect(struct DrawCtx* dc, Pair p, Pair dim, argb col) {
    XSetForeground(dc->dp, dc->screen_gc, col | 0xFF000000);
    return XFillRectangle(
        dc->dp,
        dc->back_buffer,
        dc->screen_gc,
        p.x,
        p.y,
        dim.x,
        dim.y
    );
}

int draw_line(
    struct DrawCtx* dc,
    Pair from,
    Pair to,
    enum Schm sc,
    Bool invert
) {
    XSetForeground(
        dc->dp,
        dc->screen_gc,
        invert ? COL_BG(dc, sc) : COL_FG(dc, sc)
    );
    return XDrawLine(
        dc->dp,
        dc->back_buffer,
        dc->screen_gc,
        from.x,
        from.y,
        to.x,
        to.y
    );
}

u32 get_string_width(struct DrawCtx const* dc, char const* str, u32 len) {
    XGlyphInfo ext;
    XftTextExtentsUtf8(dc->dp, dc->fnt.xfont, (XftChar8*)str, (i32)len, &ext);
    return ext.xOff;
}

u32 get_int_width(struct DrawCtx const* dc, char const* format, u32 i) {
    static u32 const MAX_BUF = 50;
    char buf[MAX_BUF];
    snprintf(buf, MAX_BUF, format, i);
    return get_string_width(dc, buf, strlen(buf));
}

void draw_selection_circle(
    struct DrawCtx* dc,
    struct SelectionCircle const* sc,
    i32 const pointer_x,
    i32 const pointer_y
) {
    if (!sc->is_active) {
        return;
    }

    i32 const outer_r = (i32)SEL_CIRC_OUTER_R_PX;
    i32 const inner_r = (i32)SEL_CIRC_INNER_R_PX;

    XSetLineAttributes(
        dc->dp,
        dc->screen_gc,
        SEL_CIRC_LINE_W,
        SEL_CIRC_LINE_STYLE,
        CapNotLast,
        JoinMiter
    );

    XSetForeground(dc->dp, dc->screen_gc, COL_BG(dc, SchmNorm));
    XFillArc(
        dc->dp,
        dc->window,
        dc->screen_gc,
        sc->x - outer_r,
        sc->y - outer_r,
        outer_r * 2,
        outer_r * 2,
        0,
        360 * 64
    );

    {
        double const segment_rad = PI * 2 / MAX(1, arrlen(sc->items_arr));
        double const segment_deg = segment_rad / PI * 180;

        // item images
        for (u32 item = 0; item < arrlen(sc->items_arr); ++item) {
            XImage* image = images[sc->items_arr[item].icon];
            assert(image != NULL);

            XPutImage(
                dc->dp,
                dc->window,
                dc->screen_gc,
                image,
                0,
                0,
                (i32)(sc->x
                      + cos(-segment_rad * (item + 0.5))
                          * ((outer_r + inner_r) * 0.5)
                      - image->width / 2.0),
                (i32)(sc->y
                      + sin(-segment_rad * (item + 0.5))
                          * ((outer_r + inner_r) * 0.5)
                      - image->height / 2.0),
                image->width,
                image->height
            );
        }

        // selected item fill
        i32 const current_item = sel_circ_curr_item(sc, pointer_x, pointer_y);
        if (current_item != NIL) {
            XSetForeground(dc->dp, dc->screen_gc, COL_BG(dc, SchmFocus));
            XFillArc(
                dc->dp,
                dc->window,
                dc->screen_gc,
                sc->x - outer_r,
                sc->y - outer_r,
                outer_r * 2,
                outer_r * 2,
                (i32)(current_item * segment_deg) * 64,
                (i32)segment_deg * 64
            );
            XSetForeground(dc->dp, dc->screen_gc, COL_BG(dc, SchmNorm));
            XFillArc(
                dc->dp,
                dc->window,
                dc->screen_gc,
                sc->x - inner_r,
                sc->y - inner_r,
                inner_r * 2,
                inner_r * 2,
                (i32)(current_item * segment_deg) * 64,
                (i32)segment_deg * 64
            );
        }

        if (arrlen(sc->items_arr) >= 2) {  // segment lines
            XSetForeground(dc->dp, dc->screen_gc, COL_FG(dc, SchmNorm));
            for (u32 line_num = 0; line_num < arrlen(sc->items_arr);
                 ++line_num) {
                XDrawLine(
                    dc->dp,
                    dc->window,
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
        XSetForeground(dc->dp, dc->screen_gc, COL_FG(dc, SchmNorm));
        XDrawArc(
            dc->dp,
            dc->window,
            dc->screen_gc,
            sc->x - inner_r,
            sc->y - inner_r,
            inner_r * 2,
            inner_r * 2,
            0,
            360 * 64
        );

        XDrawArc(
            dc->dp,
            dc->window,
            dc->screen_gc,
            sc->x - outer_r,
            sc->y - outer_r,
            outer_r * 2,
            outer_r * 2,
            0,
            360 * 64
        );
    }
}

void update_screen(struct Ctx* ctx) {
    struct DrawCtx* dc = &ctx->dc;
    /* draw canvas */ {
        fill_rect(
            dc,
            (Pair) {0, 0},
            (Pair) {(i32)dc->width, (i32)dc->height},
            WND_BACKGROUND
        );
        /* put scaled image */ {
            //  https://stackoverflow.com/a/66896097

            // resize pixmaps if needed
            if (dc->cache.pm_w != dc->cv.im->width
                || dc->cache.pm_h != dc->cv.im->height) {
                dc_cache_free(dc);
                dc_cache_init(dc);
            }

            dc_cache_update_pm(dc, dc->cache.pm, dc->cv.im);
            dc_cache_update_pm(dc, dc->cache.overlay, ctx->input.overlay);

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

            double const z = 1.0 / ZOOM_C(dc);
            XTransform xtrans_z = xtrans_scale(z);
            XTransform xtrans_ovr = xtrans_mult(
                xtrans_from_trans(OVERLAY_TRANSFORM(&ctx->input.mode)),
                xtrans_z
            );
            // HACK xrender missinterprets translate transformations
            xtrans_ovr.matrix[0][2] =
                XDoubleToFixed(-1.0 * XFixedToDouble(xtrans_ovr.matrix[0][2]));
            xtrans_ovr.matrix[1][2] =
                XDoubleToFixed(-1.0 * XFixedToDouble(xtrans_ovr.matrix[1][2]));

            XRenderSetPictureTransform(dc->dp, cv_pict, &xtrans_z);
            XRenderSetPictureTransform(dc->dp, overlay_pict, &xtrans_ovr);

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
        Rect const idmg = ctx->input.mode.d.trans.overlay_bounds;
        Pair transformed[] = {
            point_apply_trans((Pair) {idmg.l, idmg.t}, trans),
            point_apply_trans((Pair) {idmg.r + 1, idmg.t}, trans),
            point_apply_trans((Pair) {idmg.r + 1, idmg.b + 1}, trans),
            point_apply_trans((Pair) {idmg.l, idmg.b + 1}, trans),
        };
        for (u32 i = 0; i < LENGTH(transformed); ++i) {
            Pair from = point_from_cv_to_scr(dc, transformed[i]);
            Pair to = point_from_cv_to_scr(
                dc,
                transformed[(i + 1) % LENGTH(transformed)]
            );
            XSetForeground(dc->dp, dc->screen_gc, 0xFF000000);
            XSetLineAttributes(
                dc->dp,
                dc->screen_gc,
                3,
                LineOnOffDash,
                CapButt,
                JoinMiter
            );
            XDrawLine(
                dc->dp,
                dc->back_buffer,
                dc->screen_gc,
                from.x,
                from.y,
                to.x,
                to.y
            );
        }
    }

    if (WND_ANCHOR_CROSS_SIZE && !IS_PNIL(ctx->input.anchor)
        && !ctx->input.is_dragging) {
        i32 const size = WND_ANCHOR_CROSS_SIZE;
        Pair center = point_from_cv_to_scr(dc, ctx->input.anchor);
        Pair lt = (Pair) {center.x - size, center.y - size};
        Pair lb = (Pair) {center.x - size, center.y + size};
        Pair rt = (Pair) {center.x + size, center.y - size};
        Pair rb = (Pair) {center.x + size, center.y + size};
        draw_line(dc, lt, rb, SchmNorm, True);
        draw_line(dc, lb, rt, SchmNorm, True);
    }

    update_statusline(ctx);  // backbuffer swapped here
}

static u32 get_module_width(struct Ctx const* ctx, SLModule const* module) {
    struct DrawCtx const* dc = &ctx->dc;
    struct ToolCtx const* tc = &CURR_TC(ctx);

    switch (module->t) {
        case SLM_Spacer: return module->d.spacer;
        case SLM_Text:
            return get_string_width(dc, module->d.text, strlen(module->d.text));
        case SLM_ToolCtx: {
            u32 result = 0;
            for (i32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                result += get_int_width(dc, "%d", tc_name)
                    + STATUSLINE_MODULE_SPACING_SMALL_PX;
            }
            return result - STATUSLINE_MODULE_SPACING_SMALL_PX;
        }
        case SLM_Mode: {
            char const* int_str = input_mode_as_str(ctx->input.mode.t);
            return get_string_width(dc, int_str, strlen(int_str));
        }
        case SLM_Tool: {
            char const* tool_str = tc_get_tool_name(tc);
            return get_string_width(dc, tool_str, strlen(tool_str));
        }
        case SLM_ToolLineW: return get_int_width(dc, "%d", tc->line_w);
        case SLM_ToolSpacing:
            return get_int_width(
                dc,
                "%d",
                TC_IS_DRAWER(tc) ? tc->d.drawer.spacing : 0
            );
        case SLM_ColorBox: return module->d.color_box_w;
        case SLM_ColorName:
            // XXX uses 'F' as overage character
            return get_string_width(dc, "#FFFFFF", 7);
        case SLM_ColorList:
            return get_string_width(dc, "/", 1)
                + get_int_width(dc, "%d", MAX_COLORS) * 2;
    }
    UNREACHABLE();
    return 0;
}

static void draw_module(struct Ctx* ctx, SLModule const* module, Pair c) {
    struct DrawCtx* dc = &ctx->dc;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct InputMode* mode = &ctx->input.mode;

    switch (module->t) {
        case SLM_Spacer: break;
        case SLM_Text: {
            char const* str = module->d.text;
            draw_string(dc, str, c, SchmNorm, False);
        } break;
        case SLM_ToolCtx: {
            i32 x = c.x;
            for (i32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                draw_int(
                    dc,
                    tc_name,
                    (Pair) {(i32)x, c.y},
                    ctx->curr_tc == (tc_name - 1) ? SchmFocus : SchmNorm,
                    False
                );
                x += (i32)(get_int_width(dc, "%d", tc_name)
                           + STATUSLINE_MODULE_SPACING_SMALL_PX);
            }
        } break;
        case SLM_Mode: {
            char const* name = input_mode_as_str(mode->t);
            enum Schm const schm =
                mode->t == InputT_Interact ? SchmNorm : SchmFocus;
            draw_string(dc, name, c, schm, False);
        } break;
        case SLM_Tool: {
            char const* name = tc_get_tool_name(tc);
            draw_string(dc, name, c, SchmNorm, False);
        } break;
        case SLM_ToolLineW: {
            draw_int(dc, (i32)tc->line_w, c, SchmNorm, False);
        } break;
        case SLM_ToolSpacing: {
            i32 const val = TC_IS_DRAWER(tc) ? (i32)tc->d.drawer.spacing : 0;
            draw_int(dc, val, c, SchmNorm, False);
        } break;
        case SLM_ColorBox: {
            fill_rect(
                dc,
                (Pair) {c.x, clientarea_size(dc).y},
                (Pair) {(i32)module->d.color_box_w, (i32)statusline_height(dc)},
                *tc_curr_col(tc)
            );
        } break;
        case SLM_ColorName: {
            static u32 const col_value_size = 1 + 6;

            char col_value[col_value_size + 1];
            sprintf(col_value, "#%06X", *tc_curr_col(tc) & 0xFFFFFF);
            draw_string(dc, col_value, c, SchmNorm, False);
            // draw focused digit
            if (mode->t == InputT_Color) {
                static u32 const hash_w = 1;
                u32 const curr_dig = mode->d.col.current_digit;
                char const col_digit_value[] =
                    {[0] = col_value[curr_dig + hash_w], [1] = '\0'};
                u32 focus_offset =
                    get_string_width(dc, col_value, curr_dig + hash_w);
                draw_string(
                    dc,
                    col_digit_value,
                    (Pair) {c.x + (i32)focus_offset, c.y},
                    SchmFocus,
                    False
                );
            }
        } break;
        case SLM_ColorList: {
            // FIXME why it compiles
            char col_count[digit_count(MAX_COLORS) * 2 + 1 + 1];
            sprintf(col_count, "%d/%td", tc->curr_col + 1, arrlen(tc->colarr));
            draw_string(dc, col_count, c, SchmNorm, False);
        } break;
    }
}

void update_statusline(struct Ctx* ctx) {
    struct DrawCtx* dc = &ctx->dc;
    struct InputMode* mode = &ctx->input.mode;
    u32 const statusline_h = statusline_height(dc);
    Pair const clientarea = clientarea_size(dc);

    fill_rect(
        dc,
        (Pair) {0, clientarea.y},
        (Pair) {(i32)dc->width, (i32)statusline_h},
        COL_BG(&ctx->dc, SchmNorm)
    );
    if (mode->t == InputT_Console) {
        char const* command = mode->d.cl.cmdarr;
        usize const command_len = arrlen(command);
        // extra 2 for prompt and terminator
        char* cl_str_dyn = ecalloc(2 + command_len, sizeof(char));
        cl_str_dyn[0] = ':';
        cl_str_dyn[command_len + 1] = '\0';
        memcpy(cl_str_dyn + 1, command, command_len);
        i32 const user_cmd_w =
            (i32)get_string_width(&ctx->dc, cl_str_dyn, command_len + 1);
        i32 const cmd_y = (i32)(ctx->dc.height - STATUSLINE_PADDING_BOTTOM);
        draw_string(&ctx->dc, cl_str_dyn, (Pair) {0, cmd_y}, SchmNorm, False);
        if (mode->d.cl.compls_arr) {
            draw_string(
                &ctx->dc,
                mode->d.cl.compls_arr[mode->d.cl.compls_curr],
                (Pair) {user_cmd_w, cmd_y},
                SchmFocus,
                False
            );
        }
        str_free(&cl_str_dyn);
    } else {
        u32 const y = dc->height - STATUSLINE_PADDING_BOTTOM;

        {
            // current module left-bottom corner
            u32 x = 0;
            for (u32 i = 0; i < LENGTH(LEFT_MODULES); ++i) {
                SLModule const* module = &LEFT_MODULES[i];
                draw_module(ctx, module, (Pair) {(i32)x, (i32)y});

                x += get_module_width(ctx, module)
                    + STATUSLINE_MODULE_SPACING_PX;
            }
        }

        {
            // current module left-bottom corner
            u32 x = dc->width;
            for (i32 i = LENGTH(RIGHT_MODULES) - 1; i >= 0; --i) {
                SLModule const* module = &RIGHT_MODULES[i];
                x -= get_module_width(ctx, module);

                draw_module(ctx, module, (Pair) {(i32)x, (i32)y});

                x -= STATUSLINE_MODULE_SPACING_PX;
            }
        }
    }

    XdbeSwapBuffers(
        ctx->dc.dp,
        &(XdbeSwapInfo) {
            .swap_window = ctx->dc.window,
            .swap_action = 0,
        },
        1
    );

    XSyncSetCounter(
        ctx->dc.dp,
        ctx->sync_counter,
        ctx->last_sync_request_value
    );
}

// FIXME DRY
void show_message(struct Ctx* ctx, char const* msg) {
    u32 const statusline_h =
        ctx->dc.fnt.xfont->ascent + STATUSLINE_PADDING_BOTTOM;
    fill_rect(
        &ctx->dc,
        (Pair) {0, (i32)(ctx->dc.height - statusline_h)},
        (Pair) {(i32)ctx->dc.width, (i32)statusline_h},
        COL_BG(&ctx->dc, SchmNorm)
    );
    draw_string(
        &ctx->dc,
        msg,
        (Pair) {0, (i32)(ctx->dc.height - STATUSLINE_PADDING_BOTTOM)},
        SchmNorm,
        False
    );
    XdbeSwapBuffers(
        ctx->dc.dp,
        &(XdbeSwapInfo) {
            .swap_window = ctx->dc.window,
            .swap_action = 0,
        },
        1
    );
}

void dc_cache_init(struct DrawCtx* dc) {
    assert(dc->cache.pm == 0 && dc->cache.overlay == 0);

    dc->cache.pm = XCreatePixmap(
        dc->dp,
        dc->window,
        dc->cv.im->width,
        dc->cv.im->height,
        dc->sys.vinfo.depth
    );
    dc->cache.overlay = XCreatePixmap(
        dc->dp,
        dc->window,
        dc->cv.im->width,
        dc->cv.im->height,
        dc->sys.vinfo.depth
    );
    dc->cache.pm_w = dc->cv.im->width;
    dc->cache.pm_h = dc->cv.im->height;
}

void dc_cache_update_pm(struct DrawCtx* dc, Pixmap pm, XImage* im) {
    assert(im);
    XPutImage(
        dc->dp,
        pm,
        dc->screen_gc,
        im,
        0,
        0,
        0,
        0,
        dc->cv.im->width,
        dc->cv.im->height
    );
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
        fprintf(stderr, "no SYNC Extention support\n");
    }
}

void setup(Display* dp, struct Ctx* ctx) {
    assert(dp);
    assert(ctx);

    /* init arrays */ {
        for (i32 i = 0; i < TCS_NUM; ++i) {
            struct ToolCtx tc = {
                .colarr = NULL,
                .curr_col = 0,
                .prev_col = 0,
                .line_w = TOOLS_DEFAULT_LINE_W,
            };
            arrpush(ctx->tcarr, tc);
            arrpush(ctx->tcarr[i].colarr, 0xFF000000);
            arrpush(ctx->tcarr[i].colarr, 0xFFFFFFFF);
        }
    }

    /* atoms */ {
        atoms[A_Clipboard] = XInternAtom(dp, "CLIPBOARD", False);
        atoms[A_Targets] = XInternAtom(dp, "TARGETS", False);
        atoms[A_Utf8string] = XInternAtom(dp, "UTF8_STRING", False);
        atoms[A_ImagePng] = XInternAtom(dp, "image/png", False);
        atoms[A_WmProtocols] = XInternAtom(dp, "WM_PROTOCOLS", False);
        atoms[A_WmDeleteWindow] = XInternAtom(dp, "WM_DELETE_WINDOW", False);
        atoms[A_NetWmSyncRequest] =
            XInternAtom(dp, "_NET_WM_SYNC_REQUEST", False);
        atoms[A_NetWmSyncRequestCounter] =
            XInternAtom(dp, "_NET_WM_SYNC_REQUEST_COUNTER", False);
    }

    /* xrender */ {
        ctx->dc.sys.xrnd_pic_format =
            XRenderFindStandardFormat(ctx->dc.dp, PictStandardARGB32);
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

    ctx->dc.sys.colmap =
        XCreateColormap(dp, root, ctx->dc.sys.vinfo.visual, AllocNone);

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
        &(XSetWindowAttributes
        ) {.colormap = ctx->dc.sys.colmap,
           .border_pixel = 0,
           .background_pixel = WND_BACKGROUND,
           .event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask
               | ExposureMask | PointerMotionMask | StructureNotifyMask}
    );
    ctx->dc.screen_gc = XCreateGC(dp, ctx->dc.window, 0, 0);

    XSetWMName(
        dp,
        ctx->dc.window,
        &(XTextProperty
        ) {.value = (unsigned char*)title,
           .nitems = strlen(title),
           .format = 8,
           .encoding = atoms[A_Utf8string]}
    );

    /* turn on protocol support */ {
        Atom wm_delete_window = XInternAtom(dp, "WM_DELETE_WINDOW", False);

        Atom protocols[] = {wm_delete_window, atoms[A_NetWmSyncRequest]};
        XSetWMProtocols(dp, ctx->dc.window, protocols, LENGTH(protocols));
    }

    /* _NET_WM_SYNC_REQUEST */ {
        XSyncIntToValue(&ctx->last_sync_request_value, 0);
        ctx->sync_counter =
            XSyncCreateCounter(dp, ctx->last_sync_request_value);

        XChangeProperty(
            dp,
            ctx->dc.window,
            atoms[A_NetWmSyncRequestCounter],
            XA_CARDINAL,
            32,
            PropModeReplace,
            (unsigned char*)&ctx->sync_counter,
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

    if (!fnt_set(&ctx->dc, FONT_NAME)) {
        die("failed to load default font: %s", FONT_NAME);
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
                        j ? &ctx->dc.schemes_dyn[i].bg
                          : &ctx->dc.schemes_dyn[i].fg
                    )) {
                    die("can't alloc color");
                };
            }
        }
    }

    /* static images */ {
        for (i32 i = 0; i < I_Last; ++i) {
            struct IconData icon = get_icon_data(i);
            struct Image im = read_file_from_memory(
                &ctx->dc,
                icon.data,
                icon.len,
                COL_BG(&ctx->dc, SchmNorm)
            );
            images[i] = im.im;
        }
    }

    /* canvas */ {
        XGCValues canvas_gc_vals = {
            .line_style = LineSolid,
            .line_width = 5,
            .cap_style = CapButt,
            .fill_style = FillSolid
        };
        ctx->dc.gc = XCreateGC(
            dp,
            ctx->dc.window,
            GCForeground | GCBackground | GCFillStyle | GCLineStyle
                | GCLineWidth | GCCapStyle | GCJoinStyle,
            &canvas_gc_vals
        );
        // read canvas data from file or create empty
        if (ctx->inp.t != IO_None) {
            struct Image im = read_image_io(&ctx->dc, &ctx->inp, 0);
            if (!canvas_load(ctx, &im)) {
                die("failed to read input file '%s'", ioctx_as_str(&ctx->inp));
            }
        } else {
            Pixmap data = XCreatePixmap(
                dp,
                ctx->dc.window,
                ctx->dc.width,
                ctx->dc.height,
                ctx->dc.sys.vinfo.depth
            );
            ctx->dc.cv.im = XGetImage(
                dp,
                data,
                0,
                0,
                ctx->dc.width,
                ctx->dc.height,
                AllPlanes,
                ZPixmap
            );
            XFreePixmap(dp, data);
            // initial canvas color
            canvas_fill(ctx->dc.cv.im, CANVAS_BACKGROUND);
        }
        /* overlay */ {
            ctx->input.overlay = XSubImage(
                ctx->dc.cv.im,
                0,
                0,
                ctx->dc.cv.im->width,
                ctx->dc.cv.im->height
            );
            overlay_clear(ctx->input.overlay);
        }

        ctx->dc.width = CLAMP(
            ctx->dc.cv.im->width,
            WND_LAUNCH_MIN_SIZE.x,
            WND_LAUNCH_MAX_SIZE.x
        );
        ctx->dc.height = CLAMP(
            ctx->dc.cv.im->height + statusline_height(&ctx->dc),
            WND_LAUNCH_MIN_SIZE.y,
            WND_LAUNCH_MAX_SIZE.y
        );
        XResizeWindow(dp, ctx->dc.window, ctx->dc.width, ctx->dc.height);
    }

    // draw cache
    dc_cache_init(&ctx->dc);

    for (i32 i = 0; i < TCS_NUM; ++i) {
        tc_set_tool(&ctx->tcarr[i], Tool_Pencil);
    }

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
    XEvent event;

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

    if (ctx->input.mode.t == InputT_Transform) {
        // do nothing
    } else if (btn_eq(get_btn(e), BTN_SEL_CIRC)) {
        sel_circ_init_and_show(ctx, e->x, e->y);
    } else if (tc->on_press) {
        ctx->input.damage = tc->on_press(ctx, e);
    }

    update_screen(ctx);
    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    ctx->input.press_pt = point_from_scr_to_cv_xy(&ctx->dc, e->x, e->y);
    ctx->input.holding_button = get_btn(e);
    ctx->input.is_holding = True;

    return HR_Ok;
}

HdlrResult button_release_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct Input* inp = &ctx->input;
    Button e_btn = get_btn(e);

    if (btn_eq(e_btn, BTN_SCROLL_UP)) {
        canvas_scroll(&ctx->dc.cv, (Pair) {0, 10});
    } else if (btn_eq(e_btn, BTN_SCROLL_DOWN)) {
        canvas_scroll(&ctx->dc.cv, (Pair) {0, -10});
    } else if (btn_eq(e_btn, BTN_SCROLL_LEFT)) {
        canvas_scroll(&ctx->dc.cv, (Pair) {-10, 0});
    } else if (btn_eq(e_btn, BTN_SCROLL_RIGHT)) {
        canvas_scroll(&ctx->dc.cv, (Pair) {10, 0});
    } else if (btn_eq(e_btn, BTN_ZOOM_IN)) {
        canvas_change_zoom(&ctx->dc, ctx->input.prev_c, 1);
    } else if (btn_eq(e_btn, BTN_ZOOM_OUT)) {
        canvas_change_zoom(&ctx->dc, ctx->input.prev_c, -1);
    } else if (inp->mode.t == InputT_Transform) {
        struct InputTransformData* transd = &inp->mode.d.trans;
        if (btn_eq(e_btn, BTN_TRANS_MOVE)) {
            Pair cur = point_from_scr_to_cv_xy(&ctx->dc, e->x, e->y);
            transd->acc.move.x += cur.x - inp->press_pt.x;
            transd->acc.move.y += cur.y - inp->press_pt.y;
        }
        transd->curr = (Transform) {0};
    } else if (btn_eq(e_btn, BTN_SEL_CIRC)) {
        i32 const selected_item = sel_circ_curr_item(&ctx->sc, e->x, e->y);
        if (selected_item != NIL
            && ctx->sc.items_arr[selected_item].on_select) {
            ctx->sc.items_arr[selected_item].on_select(ctx);
        }
    } else if (tc->on_release) {
        Rect damage = rect_expand(tc->on_release(ctx, e), inp->damage);
        if (inp->mode.t == InputT_Transform) {
            inp->mode.d.trans.overlay_bounds = ximage_calc_damage(inp->overlay);
        } else if (!IS_RNIL(damage)) {
            history_forward(ctx, history_new_item(ctx->dc.cv.im, damage));
            overlay_dump(ctx->dc.cv.im, inp->overlay);
            overlay_clear(inp->overlay);
        }

        inp->damage = RNIL;
    }

    sel_circ_free_and_hide(&ctx->sc);
    update_screen(ctx);

    inp->is_holding = False;
    inp->is_dragging = False;
    inp->press_pt = PNIL;

    return HR_Ok;
}

HdlrResult destroy_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    return HR_Ok;
}

HdlrResult expose_hdlr(struct Ctx* ctx, XEvent* event) {
    update_screen(ctx);
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
    char* cl_msg_to_show = NULL;

    struct Input const* inp = &ctx->input;
    struct InputMode const* mode = &inp->mode;
    XKeyPressedEvent e = event->xkey;
    if (e.type == KeyRelease) {
        return HR_Ok;
    }

    Status lookup_status;
    KeySym key_sym = NoSymbol;
    char lookup_buf[32] = {0};
    i32 const text_len = Xutf8LookupString(
        ctx->dc.sys.xic,
        &e,
        lookup_buf,
        sizeof(lookup_buf) - 1,
        &key_sym,
        &lookup_status
    );

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
        && (state_match(curr.mask, ControlMask)
            || state_match(curr.mask, ControlMask | ShiftMask))) {
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
        u32 val = lookup_buf[0]
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
        if (key_eq(curr, KEY_CL_APPLY_COMPLT) && cl->compls_arr) {
            char* complt = cl->compls_arr[cl->compls_curr];
            while (*complt) {
                arrpush(cl->cmdarr, *complt);
                complt += 1;
            }
            cl_compls_free(cl);
            cl_push(cl, CL_DELIM[0]);
        } else if (key_eq(curr, KEY_CL_RUN)) {  // run command
            char* cmd_dyn = cl_cmd_get_str_dyn(cl);
            input_mode_set(ctx, InputT_Interact);
            ClCPrsResult res = cl_cmd_parse(ctx, cmd_dyn);
            str_free(&cmd_dyn);
            Bool is_exit = False;
            switch (res.t) {
                case ClCPrs_Ok: {
                    struct ClCommand* cmd = &res.d.ok;
                    ClCPrcResult res = cl_cmd_process(ctx, cmd);
                    if (res.bit_status & ClCPrc_Msg) {
                        // XXX double memory allocation
                        cl_msg_to_show = str_new(res.msg_dyn);
                        str_free(&res.msg_dyn);  // XXX member free
                    }
                    is_exit = (Bool)(res.bit_status & ClCPrc_Exit);
                } break;
                case ClCPrs_ENoArg: {
                    if (res.d.noarg.context_optdyn) {
                        cl_msg_to_show = str_new(
                            "provide %s to '%s' command",
                            res.d.noarg.arg_desc_dyn,
                            res.d.noarg.context_optdyn
                        );
                    } else {
                        cl_msg_to_show =
                            str_new("provide %s", res.d.noarg.arg_desc_dyn);
                    }
                } break;
                case ClCPrs_EInvArg: {
                    if (res.d.invarg.context_optdyn) {
                        cl_msg_to_show = str_new(
                            "%s: invalid arg '%s': %s",
                            res.d.invarg.context_optdyn,
                            res.d.invarg.arg_dyn,
                            res.d.invarg.error_dyn
                        );
                    } else {
                        cl_msg_to_show = str_new(
                            "invalid arg '%s': %s",
                            res.d.invarg.arg_dyn,
                            res.d.invarg.error_dyn
                        );
                    }
                } break;
            }

            cl_cmd_parse_res_free(&res);
            if (is_exit) {
                return HR_Quit;
            }
        } else if (key_eq(curr, KEY_CL_REQ_COMPLT) && !cl->compls_valid) {
            cl_compls_new(cl);
        } else if (key_eq(curr, KEY_CL_NEXT_COMPLT) && cl->compls_valid) {
            usize max = arrlen(cl->compls_arr);
            if (max) {
                cl->compls_curr = (cl->compls_curr + 1) % max;
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
    if (can_action(inp, curr, ACT_COPY_AREA)) {
        XSetSelectionOwner(
            ctx->dc.dp,
            atoms[A_Clipboard],
            ctx->dc.window,
            CurrentTime
        );
        Rect damage = ximage_calc_damage(inp->overlay);

        if (ctx->sel_buf.im != NULL) {
            XDestroyImage(ctx->sel_buf.im);
        }
        if (IS_RNIL(damage)) {
            // copy all canvas
            i32 const w = ctx->dc.cv.im->width;
            i32 const h = ctx->dc.cv.im->height;
            ctx->sel_buf.im = XSubImage(ctx->dc.cv.im, 0, 0, w, h);
        } else {
            // copy cropped overlay
            i32 const w = damage.r - damage.l;
            i32 const h = damage.b - damage.t;
            ctx->sel_buf.im = XSubImage(inp->overlay, damage.l, damage.t, w, h);
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
    if (can_action(inp, curr, ACT_MODE_INTERACT)
        // XXX direct action key access
        || (mode->t == InputT_Console && key_eq(curr, ACT_MODE_INTERACT.key))) {
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
        tc_set_curr_col_num(
            &CURR_TC(ctx),
            curr_col + 1 == col_num ? 0 : curr_col + 1
        );
    }
    if (can_action(inp, curr, ACT_PREV_COLOR)) {
        u32 const curr_col = CURR_TC(ctx).curr_col;
        u32 const col_num = arrlen(CURR_TC(ctx).colarr);
        assert(col_num != 0);
        tc_set_curr_col_num(
            &CURR_TC(ctx),
            curr_col == 0 ? col_num - 1 : curr_col - 1
        );
    }
    if (can_action(inp, curr, ACT_SAVE_TO_FILE)) {  // save to current file
        if (write_io(&ctx->dc, inp, ctx->dc.cv.type, &ctx->out)) {
            trace("xpaint: file saved");
        } else {
            trace("xpaint: failed to save image");
        }
    }

    // FIXME extra updates on invalid events
    update_screen(ctx);
    if (cl_msg_to_show) {
        show_message(ctx, cl_msg_to_show);
        str_free(&cl_msg_to_show);
    }

    return HR_Ok;
}

HdlrResult mapping_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XRefreshKeyboardMapping(&event->xmapping);
    return HR_Ok;
}

HdlrResult motion_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XMotionEvent* e = (XMotionEvent*)event;
    struct ToolCtx* tc = &CURR_TC(ctx);
    struct Input* inp = &ctx->input;
    Pair const cur = point_from_scr_to_cv_xy(&ctx->dc, e->x, e->y);

    if (ctx->input.is_holding) {
        if (!ctx->input.is_dragging) {
            ctx->input.is_dragging = !PAIR_EQ(cur, ctx->input.press_pt);
        }

        struct timeval current_time;
        gettimeofday(&current_time, 0x0);
        // XXX mouse scroll and drag event are shared
        u64 const elapsed_from_last =
            current_time.tv_usec - ctx->input.last_proc_drag_ev_us;

        if (btn_eq(inp->holding_button, BTN_SCROLL_DRAG)) {
            canvas_scroll(
                &ctx->dc.cv,
                (Pair) {e->x - inp->prev_c.x, e->y - inp->prev_c.y}
            );
            // last update will be in button_release_hdlr
            if (elapsed_from_last >= MOUSE_SCROLL_UPDATE_PERIOD_US) {
                ctx->input.last_proc_drag_ev_us = current_time.tv_usec;
                update_screen(ctx);
            }
        } else if (elapsed_from_last >= DRAG_EVENT_PROC_PERIOD_US) {
            if (inp->mode.t == InputT_Transform) {
                struct InputTransformData* transd = &inp->mode.d.trans;
                if (btn_eq(inp->holding_button, BTN_TRANS_MOVE)) {
                    transd->curr.move.x = cur.x - inp->press_pt.x;
                    transd->curr.move.y = cur.y - inp->press_pt.y;
                }
                update_screen(ctx);
            } else if (tc->on_drag) {
                Rect damage = tc->on_drag(ctx, e);
                if (!IS_RNIL(damage)) {
                    ctx->input.damage = rect_expand(inp->damage, damage);
                    ctx->input.last_proc_drag_ev_us = current_time.tv_usec;
                }
                // FIXME it here to draw selection tool (returns RNIL in on_drag)
                update_screen(ctx);
            }
        }
    } else {
        // FIXME unused
        if (tc->on_move) {
            Rect damage = tc->on_move(ctx, e);
            if (!IS_RNIL(damage)) {
                inp->damage = rect_expand(inp->damage, damage);
                inp->last_proc_drag_ev_us = 0;
                update_screen(ctx);
            }
        }
    }

    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    inp->prev_c.x = e->x;
    inp->prev_c.y = e->y;

    return HR_Ok;
}

HdlrResult configure_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    struct DrawCtx* dc = &ctx->dc;

    if (dc->width == event->xconfigure.width
        && dc->height == event->xconfigure.height) {
        // configure notify calls on move events too
        return HR_Ok;
    }

    dc->width = event->xconfigure.width;
    dc->height = event->xconfigure.height;
    // backbuffer resizes automatically

    Pair const cv_size = canvas_size(dc);
    Pair const clientarea = clientarea_size(dc);

    // if canvas fits in client area
    if (cv_size.x <= clientarea.x && cv_size.y <= clientarea.y) {
        // place canvas to center of screen
        dc->cv.scroll.x = (i32)(clientarea.x - cv_size.x) / 2;
        dc->cv.scroll.y = (i32)(clientarea.y - cv_size.y) / 2;
    }

    return HR_Ok;
}

HdlrResult selection_request_hdlr(struct Ctx* ctx, XEvent* event) {
    XSelectionRequestEvent request = event->xselectionrequest;

    if (XGetSelectionOwner(ctx->dc.dp, atoms[A_Clipboard]) != ctx->dc.window
        || request.selection != atoms[A_Clipboard]
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
        stbi_uc* png_imdyn = stbi_write_png_to_mem(
            rgb_dyn,
            0,
            ctx->sel_buf.im->width,
            ctx->sel_buf.im->height,
            3,
            &png_data_size
        );
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
            trace(
                "selection request handler: stbi error: %s",
                stbi_failure_reason()
            );
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

HdlrResult selection_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    static Atom target = None;
    trace("selection notify handler");

    XSelectionEvent selection = event->xselection;
    target = None;

    if (selection.property != None) {
        Atom actual_type = 0;
        i32 actual_format = 0;
        u64 bytes_after = 0;
        Atom* data_xdyn = NULL;
        u64 count = 0;
        XGetWindowProperty(
            ctx->dc.dp,
            ctx->dc.window,
            atoms[A_Clipboard],
            0,
            LONG_MAX,
            False,
            AnyPropertyType,
            &actual_type,
            &actual_format,
            &count,
            &bytes_after,
            (unsigned char**)&data_xdyn
        );

        if (selection.target == atoms[A_Targets]) {
            for (u32 i = 0; i < count; ++i) {
                Atom li = data_xdyn[i];
                // leak
                trace("Requested target: %s\n", XGetAtomName(ctx->dc.dp, li));
                if (li == atoms[A_Utf8string]) {
                    target = atoms[A_Utf8string];
                    break;
                }
            }
            if (target != None) {
                XConvertSelection(
                    ctx->dc.dp,
                    atoms[A_Clipboard],
                    target,
                    atoms[A_Clipboard],
                    ctx->dc.window,
                    CurrentTime
                );
            }
        } else if (selection.target == target) {
            // the data is in {data, count}
        }

        if (data_xdyn) {
            XFree(data_xdyn);
        }
    }

    return HR_Ok;
}

HdlrResult client_message_hdlr(struct Ctx* ctx, XEvent* event) {
    XClientMessageEvent* e = (XClientMessageEvent*)event;
    if (e->message_type != atoms[A_WmProtocols]) {
        return HR_Ok;
    }

    if (e->data.l[0] == atoms[A_WmDeleteWindow]) {
        return HR_Quit;
    }

    if (e->data.l[0] == atoms[A_NetWmSyncRequest]) {
        ctx->last_sync_request_value =
            (XSyncValue) {.lo = e->data.l[2], .hi = (i32)e->data.l[3]};
        return HR_Ok;
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
    /* sync counter */ { XSyncDestroyCounter(ctx->dc.dp, ctx->sync_counter); }
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
        for (i32 i = 0; i < TCS_NUM; ++i) {
            tc_free(&ctx->tcarr[i]);
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
                        j ? &ctx->dc.schemes_dyn[i].bg
                          : &ctx->dc.schemes_dyn[i].fg
                    );
                }
            }
            free(ctx->dc.schemes_dyn);
        }
        fnt_free(ctx->dc.dp, &ctx->dc.fnt);
        canvas_free(&ctx->dc.cv);
        XdbeDeallocateBackBufferName(ctx->dc.dp, ctx->dc.back_buffer);
        XFreeGC(ctx->dc.dp, ctx->dc.gc);
        XFreeGC(ctx->dc.dp, ctx->dc.screen_gc);
        XFreeColormap(ctx->dc.dp, ctx->dc.sys.colmap);
        XDestroyWindow(ctx->dc.dp, ctx->dc.window);
    }
}
