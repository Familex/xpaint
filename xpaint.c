#include <X11/X.h>
#include <X11/Xatom.h>  // XA_*
#include <X11/Xft/Xft.h>
#include <X11/Xft/XftCompat.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdbe.h>  // back buffer
#include <X11/extensions/Xrender.h>
#include <X11/extensions/render.h>
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

// embeded data
INCBIN(u8, pic_tool_fill, "res/tool-fill.png");
INCBIN(u8, pic_tool_pencil, "res/tool-pencil.png");
INCBIN(u8, pic_tool_picker, "res/tool-picker.png");
INCBIN(u8, pic_tool_select, "res/tool-select.png");
INCBIN(u8, pic_unknown, "res/unknown.png");

/*
 * free -dyn vars with 'free' function
 * free -arr vars with 'arrfree' function
 * free -imdyn vars with 'stbi_image_free' function
 * free -xdyn vars with 'XFree' function
 * structs with t and d fields are tagged unions
 */

#define XLeftMouseBtn    Button1
#define XMiddleMouseBtn  Button2
#define XRightMouseBtn   Button3
#define XMouseScrollUp   Button4
#define XMouseScrollDown Button5
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#define CLAMP(X, L, H)   (((X) < (L)) ? (L) : ((X) > (H)) ? (H) : (X))
#define LENGTH(X)        (sizeof(X) / sizeof(X)[0])
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))
#define COALESCE(A, B)   ((A) ? (A) : (B))
#define PI               (3.141)
// default value for signed integers
#define NIL              (-1)

#define CURR_TC(p_ctx)     ((p_ctx)->tcarr[(p_ctx)->curr_tc])
#define CURR_COL(p_tc)     ((p_tc)->sdata.col_argbarr[(p_tc)->sdata.curr_col])
// XXX workaround
#define COL_FG(p_dc, p_sc) ((p_dc)->schemes_dyn[(p_sc)].fg.pixel | 0xFF000000)
#define COL_BG(p_dc, p_sc) ((p_dc)->schemes_dyn[(p_sc)].bg.pixel | 0xFF000000)
#define HAS_SELECTION(p_tc) \
    ((p_tc)->t == Tool_Selection && (p_tc)->d.sel.ex != NIL \
     && (p_tc)->d.sel.ey != NIL && (p_tc)->d.sel.bx != NIL \
     && (p_tc)->d.sel.by != NIL && (p_tc)->d.sel.ex != (p_tc)->d.sel.bx \
     && (p_tc)->d.sel.ey != (p_tc)->d.sel.by)
#define SELECTION_DRAGGING(p_tc) \
    ((p_tc)->t == Tool_Selection && (p_tc)->d.sel.drag_from.x != NIL \
     && (p_tc)->d.sel.drag_from.y != NIL)
#define UNREACHABLE() __builtin_unreachable()

enum {
    A_Clipboard,
    A_Targets,
    A_Utf8string,
    A_ImagePng,
    A_Last,
};

enum Icon {
    I_Select,
    I_Pencil,
    I_Fill,
    I_Picker,
    I_Last,
};

enum ImageType {
    IMT_Png,
    IMT_Jpg,
    IMT_Unknown,
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
                ClCDS_FInp,
                ClCDS_FOut,
                ClCDS_Last,
            } t;
            union ClCDSData {
                struct ClCDSDLineW {
                    u32 value;
                } line_w;
                struct ClCDSDCol {
                    u32 argb;
                } col;
                struct ClCDSDFont {
                    char* name_dyn;
                } font;
                struct ClCDSDFInp {
                    char* path_dyn;
                } finp;
                struct ClCDSDFOut {
                    char* path_dyn;
                } fout;
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

struct Ctx {
    struct DrawCtx {
        Display* dp;
        XVisualInfo vinfo;
        XIM xim;
        XIC xic;
        XRenderPictFormat* xrnd_pic_format;
        GC gc;
        GC screen_gc;
        Window window;
        u32 width;
        u32 height;
        XdbeBackBuffer back_buffer;  // double buffering
        struct Canvas {
            XImage* im;
            enum ImageType type;
            u32 width;
            u32 height;
            u32 zoom;  // 1 == no zoom
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
        Colormap colmap;
        struct Cache {
            u32 pm_w;  // to validate pm
            u32 pm_h;
            Pixmap pm;  // pixel buffer to update screen
        } cache;
    } dc;
    struct Input {
        Pair prev_c;
        Pair last_processed_pointer;
        u32 holding_button;
        u64 last_proc_drag_ev_us;
        Bool is_holding;
        Bool is_dragging;
        enum InputTag {
            InputT_Interact,
            InputT_Color,
            InputT_Console,
        } t;
        union InputData {
            struct InputColorData {
                u32 current_digit;
            } col;
            struct InputConsoleData {
                char* cmdarr;
                char** compls_arr;
                Bool compls_valid;
                usize compls_curr;
            } cl;
        } d;
    } input;
    struct ToolCtx {
        void (*on_press)(struct DrawCtx*, struct ToolCtx*, struct Input*, XButtonPressedEvent const*);
        void (*on_release)(struct DrawCtx*, struct ToolCtx*, struct Input*, XButtonReleasedEvent const*);
        void (*on_drag)(struct DrawCtx*, struct ToolCtx*, struct Input*, XMotionEvent const*);
        void (*on_move)(struct DrawCtx*, struct ToolCtx*, struct Input*, XMotionEvent const*);
        char* tool_name_dyn;
        enum ToolTag {
            Tool_Selection,
            Tool_Pencil,
            Tool_Fill,
            Tool_Picker,
        } t;
        struct ToolSharedData {
            u32* col_argbarr;
            u32 curr_col;
            u32 line_w;
        } sdata;
        union ToolData {
            struct SelectionData {
                // selection begin
                i32 by, bx;
                // selection end
                i32 ey, ex;
                // NIL if selection not dragging
                Pair drag_from, drag_to;
            } sel;
            struct PencilData {
                u32 line_r;
            } pencil;
        } d;
    }* tcarr;
    u32 curr_tc;
    struct History {
        struct Canvas cv;
    } *hist_prevarr, *hist_nextarr;
    struct SelectionCircle {
        Bool is_active;
        i32 x;
        i32 y;
        u32 item_count;
        struct Item {
            void (*on_select)(struct ToolCtx*);
            i32 hicon;  // I_*
        }* items;
    } sc;
    struct SelectionBuffer {
        XImage* im;
    } sel_buf;
    struct FileCtx {
        char* path_dyn;
    } finp, fout;
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
        ClCPrs_ENoSubArg,
        ClCPrs_EInvSubArg,
    } t;
    union {
        struct ClCommand ok;
        struct {
            char* arg_dyn;
        } invarg;
        struct {
            char* arg_dyn;
        } nosubarg;
        struct {
            char* arg_dyn;
            char* inv_val_dyn;
        } invsubarg;
    } d;
} ClCPrsResult;

// clang-format off
static void die(char const* errstr, ...);
static void trace(char const* fmt, ...);
static void* ecalloc(u32 n, u32 size);
static u32 digit_count(u32 number);
static void arrpoputf8(char const* strarr);
// needs to be 'free'd after use
static char* str_new(char const* fmt, ...);
static char* str_new_va(char const* fmt, va_list args);
static void str_free(char** str_dyn);
static usize first_dismatch(char const* restrict s1, char const* restrict s2);

static Bool fnt_set(struct DrawCtx* dc, char const* font_name);
static void fnt_free(Display* dp, struct Fnt* fnt);
static void file_ctx_set(struct FileCtx* file_ctx, char const* file_path);
static void file_ctx_free(struct FileCtx* file_ctx);

static Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p);
static Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Pair point_from_cv_to_scr_no_move(struct DrawCtx const* dc, Pair p);
static Pair point_from_scr_to_cv(struct DrawCtx const* dc, Pair p);
static Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Bool point_in_rect(Pair p, Pair a1, Pair a2);

static XImage* read_file_from_memory(struct DrawCtx const* dc, u8 const* data, u32 len, u32 transp_argb);
static XImage* read_file_from_path(struct DrawCtx const* dc, char const* file_name, u32 transp_argb);
static Bool save_file(struct DrawCtx* dc, enum ImageType type, char const* file_path);
static enum ImageType file_type(char const* file_path);
static u8* ximage_to_rgb(XImage const* image, Bool rgba);

static ClCPrcResult cl_cmd_process(struct Ctx* ctx, struct ClCommand const* cl_cmd);
static ClCPrsResult cl_cmd_parse(struct Ctx* ctx, char const* cl);
static void cl_cmd_parse_res_free(ClCPrsResult* res);
static char* cl_cmd_get_str_dyn(struct InputConsoleData const* d_cl);
static char const* cl_cmd_from_enum(enum ClCTag t);
static char const* cl_set_prop_from_enum(enum ClCDSTag t);
static char const* cl_save_type_from_enum(enum ClCDSv t);
static enum ImageType cl_save_type_to_image_type(enum ClCDSv t);
static void cl_compls_update(struct InputConsoleData* cl);
static void cl_free(struct InputConsoleData* cl);
static void cl_compls_free(struct InputConsoleData* cl);
static void cl_push(struct InputConsoleData* cl, char c);
static void cl_pop(struct InputConsoleData* cl);

static void init_sel_circ_tools(struct SelectionCircle* sc, i32 x, i32 y);
static void free_sel_circ(struct SelectionCircle* sel_circ);
static i32 current_sel_circ_item(struct SelectionCircle const* sc, i32 x, i32 y);

// separate functions, because they are callbacks
static void tool_ctx_set_selection(struct ToolCtx* tc);
static void tool_ctx_set_pencil(struct ToolCtx* tc);
static void tool_ctx_set_fill(struct ToolCtx* tc);
static void tool_ctx_set_picker(struct ToolCtx* tc);
static void tool_ctx_free(struct ToolCtx* tc);

static void input_state_set(struct Input* input, enum InputTag is);

// separate functions, because they are callbacks
static void tool_selection_on_press(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XButtonPressedEvent const* event);
static void tool_selection_on_release(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XButtonReleasedEvent const* event);
static void tool_selection_on_drag(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XMotionEvent const* event);
static void tool_pencil_on_press(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XButtonPressedEvent const* event);
static void tool_pencil_on_release(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XButtonReleasedEvent const* event);
static void tool_pencil_on_drag(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XMotionEvent const* event);
static void tool_pencil_on_move(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XMotionEvent const* event);
static void tool_fill_on_release(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XButtonReleasedEvent const* event);
static void tool_picker_on_release(struct DrawCtx* dc, struct ToolCtx* tc, struct Input* inp, XButtonReleasedEvent const* event);

static Bool history_move(struct Ctx* ctx, Bool forward);
static Bool history_push(struct History** hist, struct Ctx* ctx);

static void historyarr_clear(Display* dp, struct History** hist);

static void canvas_fill(struct DrawCtx* dc, u32 color);
static void canvas_line(struct DrawCtx* dc, Pair from, Pair to, u32 color, u32 line_w);
static void canvas_point(struct DrawCtx* dc, Pair c, u32 col, u32 line_w);
static void canvas_circle(struct DrawCtx* dc, Pair center, u32 r, u32 col, Bool fill);
static void canvas_copy_region(struct DrawCtx* dc, Pair from, Pair dims, Pair to, Bool clear_source);
static void canvas_fill_rect(struct DrawCtx* dc, Pair c, Pair dims, u32 color);
static Bool canvas_load(struct DrawCtx* dc, char const* file_name, u32 transp_argb);
static void canvas_free(Display* dp, struct Canvas* cv);
static void canvas_change_zoom(struct DrawCtx* dc, Pair cursor, i32 delta);
static void canvas_resize(struct DrawCtx* dc, i32 new_width, i32 new_height);

static u32 get_statusline_height(struct DrawCtx* dc);
static void draw_selection_circle(struct DrawCtx* dc, struct SelectionCircle const* sc, i32 pointer_x, i32 pointer_y);
static void update_screen(struct Ctx* ctx);
static void update_statusline(struct Ctx* ctx);
static void show_message(struct Ctx* ctx, char const* msg);
static void show_message_va(struct Ctx* ctx, char const* fmt, ...);

static struct Ctx ctx_init(Display* dp);
static void setup(Display* dp, struct Ctx* ctx);
static void run(struct Ctx* ctx);
static Bool button_press_hdlr(struct Ctx* ctx, XEvent* event);
static Bool button_release_hdlr(struct Ctx* ctx, XEvent* event);
static Bool destroy_notify_hdlr(struct Ctx* ctx, XEvent* event);
static Bool expose_hdlr(struct Ctx* ctx, XEvent* event);
static Bool key_press_hdlr(struct Ctx* ctx, XEvent* event);
static Bool mapping_notify_hdlr(struct Ctx* ctx, XEvent* event);
static Bool motion_notify_hdlr(struct Ctx* ctx, XEvent* event);
static Bool configure_notify_hdlr(struct Ctx* ctx, XEvent* event);
static Bool selection_request_hdlr(struct Ctx* ctx, XEvent* event);
static Bool selection_notify_hdlr(struct Ctx* ctx, XEvent* event);
static Bool client_message_hdlr(struct Ctx* ctx, XEvent* event);
static void cleanup(struct Ctx* ctx);

static void main_arg_bound_check(char const* cmd_name, i32 argc, char** argv, u32 pos);
// clang-format on

static Bool is_verbose_output = False;
static Atom atoms[A_Last];
static XImage* images[I_Last];

i32 main(i32 argc, char** argv) {
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        die("xpaint: cannot open X display");
    }

    struct Ctx ctx = ctx_init(display);

    for (i32 i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {  // main argument
            file_ctx_set(&ctx.finp, argv[i]);
            file_ctx_set(&ctx.fout, argv[i]);
        } else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            die("xpaint " VERSION);
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
            is_verbose_output = True;
        } else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--input")) {
            main_arg_bound_check("-i or --input", argc, argv, i);
            file_ctx_set(&ctx.finp, argv[++i]);
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            main_arg_bound_check("-o or --output", argc, argv, i);
            file_ctx_set(&ctx.fout, argv[++i]);
        } else if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--width")) {
            main_arg_bound_check("-w or --width", argc, argv, i);
            ctx.dc.cv.width = strtol(argv[++i], NULL, 0);
            if (ctx.dc.cv.width <= 0) {
                die("xpaint: canvas width must be positive number");
            }
            ctx.dc.width = ctx.dc.cv.width;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--height")) {
            main_arg_bound_check("-h or --height", argc, argv, i);
            ctx.dc.cv.height = strtol(argv[++i], NULL, 0);
            if (ctx.dc.cv.height <= 0) {
                die("xpaint: canvas height must be positive number");
            }
            ctx.dc.height = ctx.dc.cv.height;
        } else {
            die("Usage: xpaint [OPTIONS] [FILE]\n"
                "\n"
                "Options:\n"
                "      --help                   Print help message\n"
                "  -V, --version                Print version\n"
                "  -v, --verbose                Use verbose output\n"
                "  -w, --width <canvas width>   Set canvas width\n"
                "  -h, --height <canvas height> Set canvas height\n"
                "  -i, --input <file path>      Set load file\n"
                "  -o, --output <file path>     Set save file");
        }
    }

    /* extentions support */ {
        i32 maj = NIL;
        i32 min = NIL;
        if (!XdbeQueryExtension(display, &maj, &min)) {
            die("no X Double Buffer Extention support");
        }
    }

    setup(display, &ctx);
    run(&ctx);
    cleanup(&ctx);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
}

void die(char const* errstr, ...) {
    va_list ap;

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

usize first_dismatch(char const* restrict s1, char const* restrict s2) {
    if (!s1 || !s2) {
        return 0;
    }
    usize offset = 0;
    for (; s1[offset] && s1[offset] == s2[offset]; ++offset) {};
    return offset;
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

void file_ctx_set(struct FileCtx* file_ctx, char const* file_path) {
    file_ctx_free(file_ctx);
    file_ctx->path_dyn = str_new("%s", file_path);
}

void file_ctx_free(struct FileCtx* file_ctx) {
    str_free(&file_ctx->path_dyn);
}

void tool_ctx_free(struct ToolCtx* tc) {
    arrfree(tc->sdata.col_argbarr);
    str_free(&tc->tool_name_dyn);
}

Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p) {
    return point_from_cv_to_scr_xy(dc, p.x, p.y);
}

Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = x * (i32)dc->cv.zoom + dc->cv.scroll.x,
        .y = y * (i32)dc->cv.zoom + dc->cv.scroll.y,
    };
}

Pair point_from_cv_to_scr_no_move(struct DrawCtx const* dc, Pair p) {
    return (Pair) {.x = p.x * (i32)dc->cv.zoom, .y = p.y * (i32)dc->cv.zoom};
}

Pair point_from_scr_to_cv(struct DrawCtx const* dc, Pair p) {
    return point_from_scr_to_cv_xy(dc, p.x, p.y);
}

Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = (x - dc->cv.scroll.x) / (i32)dc->cv.zoom,
        .y = (y - dc->cv.scroll.y) / (i32)dc->cv.zoom,
    };
}

static Bool point_in_rect(Pair p, Pair a1, Pair a2) {
    return MIN(a1.x, a2.x) < p.x && p.x < MAX(a1.x, a2.x)
        && MIN(a1.y, a2.y) < p.y && p.y < MAX(a1.y, a2.y);
}

static XImage* read_file_from_memory(
    struct DrawCtx const* dc,
    u8 const* data,
    u32 len,
    u32 transp_argb
) {
    i32 width = NIL;
    i32 height = NIL;
    i32 comp = NIL;
    stbi_uc* image_data =
        stbi_load_from_memory(data, (i32)len, &width, &height, &comp, 4);
    if (image_data == NULL) {
        return NULL;
    }
    // process image data
    for (i32 i = 0; i < 4 * (width * height - 1); i += 4) {
        if (!image_data[i] && transp_argb) {
            // fill transparent pixels with transp_argb value
            image_data[i + 0] = (transp_argb & 0x000000FF) >> (0 * 8);
            image_data[i + 1] = (transp_argb & 0x0000FF00) >> (1 * 8);
            image_data[i + 2] = (transp_argb & 0x00FF0000) >> (2 * 8);
            image_data[i + 3] = (transp_argb & 0xFF000000) >> (3 * 8);
        } else {
            // rgb -> bgr
            u32 const red = image_data[i + 2];
            u32 const blue = image_data[i + 0];
            image_data[i + 2] = blue;
            image_data[i + 0] = red;
        }
    }
    XImage* result = XCreateImage(
        dc->dp,
        dc->vinfo.visual,
        dc->vinfo.depth,
        ZPixmap,
        0,
        (char*)image_data,
        width,
        height,
        32,  // FIXME what is it? (must be 32)
        width * 4
    );

    return result;
}

XImage* read_file_from_path(
    struct DrawCtx const* dc,
    char const* file_name,
    u32 transp_argb
) {
    int fd = open(file_name, O_RDONLY);
    off_t len = lseek(fd, 0, SEEK_END);
    void* data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);

    XImage* result = read_file_from_memory(dc, data, len, transp_argb);

    close(fd);

    return result;
}

Bool save_file(struct DrawCtx* dc, enum ImageType type, char const* file_path) {
    if (type == IMT_Unknown) {
        return False;
    }
    u8* rgba_dyn = ximage_to_rgb(dc->cv.im, True);
    int (*write_im)(char const*, int, int, int, void const*, int) = NULL;
    switch (type) {
        case IMT_Png: write_im = &stbi_write_png; break;
        case IMT_Jpg: write_im = &stbi_write_jpg; break;
        case IMT_Unknown: UNREACHABLE();
    }
    Bool const result = write_im(
        file_path,
        (i32)dc->cv.width,
        (i32)dc->cv.height,
        PNG_SAVE_COMPRESSION,
        rgba_dyn,
        0
    );
    free(rgba_dyn);
    return result;
}

enum ImageType file_type(char const* file_path) {
    if (!file_path) {
        return IMT_Unknown;
    }
    static u32 const HEADER_SIZE = 8;  // read extra symbols for simplicity
    u8 h[HEADER_SIZE];
    enum ImageType result = IMT_Unknown;

    FILE* file = fopen(file_path, "r");
    if (!file) {
        return IMT_Unknown;
    }
    // on file read error just fail on header check
    fread(h, sizeof(u8), HEADER_SIZE, file);

    // jpeg SOI marker and another marker begin
    if (h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) {
        result = IMT_Jpg;
    } else
        // png header
        if (h[0] == 0x89 && h[1] == 0x50 && h[2] == 0x4E && h[3] == 0x47
            && h[4] == 0x0D && h[5] == 0x0A && h[6] == 0x1A && h[7] == 0x0A) {
            result = IMT_Png;
        }

    fclose(file);
    return result;
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
                data[ii + 3] = (pixel & 0xFF000000) >> 24U;
            }
            ii += (i32)pixel_size;
        }
    }
    return data;
}

ClCPrcResult cl_cmd_process(struct Ctx* ctx, struct ClCommand const* cl_cmd) {
    assert(cl_cmd);
    char* msg_to_show = NULL;  // counts as PCCR_Msg at func end
    usize bit_status = 0;
    switch (cl_cmd->t) {
        case ClC_Set: {
            switch (cl_cmd->d.set.t) {
                case ClCDS_LineW: {
                    CURR_TC(ctx).sdata.line_w = cl_cmd->d.set.d.line_w.value;
                } break;
                case ClCDS_Col: {
                    CURR_COL(&CURR_TC(ctx)) = cl_cmd->d.set.d.col.argb;
                } break;
                case ClCDS_Font: {
                    char const* font = cl_cmd->d.set.d.font.name_dyn;
                    if (!fnt_set(&ctx->dc, font)) {
                        msg_to_show = str_new("invalid font name: '%s'", font);
                    }
                } break;
                case ClCDS_FInp: {
                    char const* path = cl_cmd->d.set.d.finp.path_dyn;
                    file_ctx_set(&ctx->finp, path);
                    msg_to_show = str_new("finp set to '%s'", path);
                } break;
                case ClCDS_FOut: {
                    char const* path = cl_cmd->d.set.d.fout.path_dyn;
                    file_ctx_set(&ctx->fout, path);
                    msg_to_show = str_new("fout set to '%s'", path);
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
            char const* path =
                COALESCE(cl_cmd->d.save.path_dyn, ctx->fout.path_dyn);
            msg_to_show = str_new(
                save_file(
                    &ctx->dc,
                    cl_save_type_to_image_type(cl_cmd->d.save.im_type),
                    path
                )
                    ? "image saved to '%s'"
                    : "failed save image to '%s'",
                path
            );
        } break;
        case ClC_Load: {
            char const* path =
                COALESCE(cl_cmd->d.load.path_dyn, ctx->finp.path_dyn);
            msg_to_show = str_new(
                canvas_load(&ctx->dc, path, 0) ? "image loaded from '%s'"
                                               : "failed load image from '%s'",
                path
            );
        } break;
        case ClC_Last: assert(!"invalid enum value");
    }
    bit_status |= msg_to_show ? ClCPrc_Msg : 0;
    return (ClCPrcResult) {.bit_status = bit_status, .msg_dyn = msg_to_show};
}

ClCPrsResult cl_cmd_parse(struct Ctx* ctx, char const* cl) {
    assert(cl);
    ClCPrsResult res = {.t = ClCPrs_Ok};
    char* cl_bufdyn = str_new("%s", cl);
    // naive split by spaces (0x20) works on utf8
    char const* cmd = strtok(cl_bufdyn, " ");
    // FIXME separate to function to able to use 'return'
    if (!cmd) {
        res.t = ClCPrs_ENoArg;
    } else if (!strcmp(cmd, "echo")) {
        res.d.ok.t = ClC_Echo;
        char const* user_msg = strtok(NULL, "");
        res.d.ok.d.echo.msg_dyn = str_new("%s", COALESCE(user_msg, ""));
    } else if (!strcmp(cmd, cl_cmd_from_enum(ClC_Set))) {
        res.d.ok.t = ClC_Set;
        res.d.ok.d.set = (struct ClCDSet) {0};
        char const* prop = strtok(NULL, " ");
        if (!prop) {
            // can't return there, because we must free resourses
            res.t = ClCPrs_ENoSubArg;
            res.d.nosubarg.arg_dyn = str_new(cl_cmd_from_enum(ClC_Set));
        } else if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_LineW))) {
            res.d.ok.d.set.t = ClCDS_LineW;
            char const* args = strtok(NULL, "");
            res.d.ok.d.set.d.line_w.value =
                args ? strtol(args, NULL, 0) : TOOLS.default_line_w;
        } else if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Col))) {
            res.d.ok.d.set.t = ClCDS_Col;
            res.d.ok.d.set.d.col.argb =
                (strtol(strtok(NULL, ""), NULL, 16) & 0xFFFFFF) | 0xFF000000;
        } else if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_Font))) {
            res.d.ok.d.set.t = ClCDS_Font;
            char const* font = strtok(NULL, " ");
            if (font) {
                res.d.ok.d.set.d.font.name_dyn = str_new("%s", font);
            } else {
                res.t = ClCPrs_ENoSubArg;
                res.d.nosubarg.arg_dyn =
                    str_new(cl_set_prop_from_enum(ClCDS_Font));
            }
        } else if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_FInp))) {
            res.d.ok.d.set.t = ClCDS_FInp;
            char const* path = strtok(NULL, "");  // user can load NULL
            if (path) {
                res.d.ok.d.set.d.finp.path_dyn = str_new("%s", path);
            }
        } else if (!strcmp(prop, cl_set_prop_from_enum(ClCDS_FOut))) {
            res.d.ok.d.set.t = ClCDS_FOut;
            char const* path = strtok(NULL, "");  // user can load NULL
            if (path) {
                res.d.ok.d.set.d.fout.path_dyn = str_new("%s", path);
            }
        } else {
            res.t = ClCPrs_EInvSubArg;
            res.d.invsubarg.arg_dyn = str_new(cl_cmd_from_enum(ClC_Set));
            res.d.invsubarg.inv_val_dyn = str_new(prop);
        }
    } else if (!strcmp(cmd, cl_cmd_from_enum(ClC_Exit))) {
        res.d.ok.t = ClC_Exit;
    } else if (!strcmp(cmd, cl_cmd_from_enum(ClC_Save))) {
        res.d.ok.t = ClC_Save;
        char const* type_str = strtok(NULL, " ");
        if (!type_str) {
            res.t = ClCPrs_ENoSubArg;
            res.d.nosubarg.arg_dyn = str_new(cl_cmd_from_enum(ClC_Save));
        } else {
            enum ClCDSv* type = &res.d.ok.d.save.im_type;
            for (*type = 0; *type < ClCDSv_Last; ++*type) {
                if (!strcmp(type_str, cl_save_type_from_enum(*type))) {
                    break;
                }
            }
            if (*type == ClCDSv_Last) {
                res.t = ClCPrs_EInvSubArg;
                res.d.invsubarg.arg_dyn = str_new(cl_cmd_from_enum(ClC_Save));
                res.d.invsubarg.inv_val_dyn = str_new("%s", type_str);
            } else {
                char const* path = strtok(NULL, "");  // include spaces
                if (path) {
                    res.d.ok.d.save.path_dyn = str_new("%s", path);
                }
            }
        }
    } else if (!strcmp(cmd, cl_cmd_from_enum(ClC_Load))) {
        res.d.ok.t = ClC_Load;
        char const* path = strtok(NULL, "");  // path with spaces
        if (path) {
            res.d.ok.d.load.path_dyn = str_new("%s", path);
        }
    } else {
        res.t = ClCPrs_EInvArg;
        res.d.invarg.arg_dyn = str_new("%s", cmd);
    }
    str_free(&cl_bufdyn);

    return res;
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
                        case ClCDS_FInp:
                            free(cl_cmd->d.set.d.finp.path_dyn);
                            break;
                        case ClCDS_FOut:
                            free(cl_cmd->d.set.d.fout.path_dyn);
                            break;
                        case ClCDS_LineW:
                        case ClCDS_Col:
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
        } break;
        case ClCPrs_EInvSubArg: {
            str_free(&res->d.invsubarg.arg_dyn);
            str_free(&res->d.invsubarg.inv_val_dyn);
        } break;
        case ClCPrs_ENoSubArg: {
            str_free(&res->d.nosubarg.arg_dyn);
        } break;
        case ClCPrs_ENoArg: break;
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
        case ClCDS_FInp: return "finp";
        case ClCDS_FOut: return "fout";
        case ClCDS_Font: return "font";
        case ClCDS_LineW: return "line_w";
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
    usize enum_last
) {
    for (u32 e = 0; e < enum_last; ++e) {
        char const* enum_str = enum_to_str(e);
        usize offset = first_dismatch(enum_str, token);
        if (offset == strlen(token)) {
            arrpush(*result, str_new("%s", enum_str + offset));
        }
    }
}

void cl_compls_update(struct InputConsoleData* cl) {
    char* cl_buf_dyn = cl_cmd_get_str_dyn(cl);
    char** result = NULL;

    // XXX separator hardcoded
    char const* tok1 = strtok(cl_buf_dyn, " ");
    char const* tok2 = strtok(NULL, " ");  // can be NULL
    tok1 = COALESCE(tok1, "");  // don't do strtok in macro
    tok2 = COALESCE(tok2, "");

    typedef char const* (*cast)(usize);
    // subcommands with own completions
    if (!strcmp(tok1, cl_cmd_from_enum(ClC_Set))) {
        cl_compls_update_helper(
            &result,
            tok2,
            (cast)&cl_set_prop_from_enum,
            ClCDS_Last
        );
    } else if (!strcmp(tok1, cl_cmd_from_enum(ClC_Save))) {
        cl_compls_update_helper(
            &result,
            tok2,
            (cast)&cl_save_type_from_enum,
            ClCDSv_Last
        );
    } else {  // first token comletion
        cl_compls_update_helper(
            &result,
            tok1,
            (cast)&cl_cmd_from_enum,
            ClC_Last
        );
    }

    free(cl_buf_dyn);

    cl_compls_free(cl);
    cl->compls_arr = result;
    cl->compls_valid = True;
    cl->compls_curr = 0;  // prevent out-of-range errors
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
    }
}

void cl_push(struct InputConsoleData* cl, char c) {
    arrpush(cl->cmdarr, c);
    cl->compls_valid = False;
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
    cl->compls_valid = False;
    cl_compls_free(cl);
}

static void set_current_tool(struct ToolCtx* tc, enum ToolTag type) {
    struct ToolCtx new_tc = {
        .t = type,
        .sdata = tc->sdata,
    };
    tc->sdata = (struct ToolSharedData) {0};  // don't let sdata be freed
    switch (type) {
        case Tool_Selection:
            new_tc.on_press = &tool_selection_on_press;
            new_tc.on_release = &tool_selection_on_release;
            new_tc.on_drag = &tool_selection_on_drag;
            new_tc.tool_name_dyn = str_new("selection");
            new_tc.d.sel = (struct SelectionData) {
                .by = NIL,
                .bx = NIL,
                .ey = NIL,
                .ex = NIL,
                .drag_from = {NIL, NIL},
                .drag_to = {NIL, NIL},
            };
            break;
        case Tool_Pencil:
            new_tc.on_press = &tool_pencil_on_press;
            new_tc.on_release = &tool_pencil_on_release;
            new_tc.on_drag = &tool_pencil_on_drag;
            new_tc.on_move = &tool_pencil_on_move;
            new_tc.tool_name_dyn = str_new("pencil");
            new_tc.d.pencil = (struct PencilData) {
                .line_r = 5,
            };
            break;
        case Tool_Fill:
            new_tc.on_release = &tool_fill_on_release;
            new_tc.tool_name_dyn = str_new("fill");
            break;
        case Tool_Picker:
            new_tc.on_release = &tool_picker_on_release;
            new_tc.tool_name_dyn = str_new("color picker");
            break;
    }
    tool_ctx_free(tc);
    *tc = new_tc;
}

void tool_ctx_set_selection(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Selection);
}

void tool_ctx_set_pencil(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Pencil);
}

void tool_ctx_set_fill(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Fill);
}

void tool_ctx_set_picker(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Picker);
}

void input_state_set(struct Input* input, enum InputTag const is) {
    switch (input->t) {
        case InputT_Console: cl_free(&input->d.cl); break;
        default: break;
    }

    input->t = is;

    switch (is) {
        case InputT_Color:
            input->d.col = (struct InputColorData) {.current_digit = 0};
            break;
        case InputT_Console:
            input->d.cl = (struct InputConsoleData
            ) {.cmdarr = NULL, .compls_valid = False};
            break;
        case InputT_Interact: break;
    }
}

void tool_selection_on_press(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XButtonPressedEvent const* event
) {
    assert(tc->t == Tool_Selection);
    if (event->button == XLeftMouseBtn) {
        struct SelectionData* sd = &tc->d.sel;
        Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
        if (HAS_SELECTION(tc)
            && point_in_rect(
                pointer,
                (Pair) {sd->bx, sd->by},
                (Pair) {sd->ex, sd->ey}
            )) {
            sd->drag_from = pointer;
            sd->drag_to = pointer;
        } else {
            sd->bx = CLAMP(pointer.x, 0, dc->cv.width);
            sd->by = CLAMP(pointer.y, 0, dc->cv.height);
            sd->ex = NIL;
            sd->ey = NIL;
        }
    }
}

void tool_selection_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XButtonReleasedEvent const* event
) {
    assert(tc->t == Tool_Selection);
    if (event->button != XLeftMouseBtn) {
        return;
    }
    struct SelectionData* sd = &tc->d.sel;

    if (SELECTION_DRAGGING(tc)) {
        // finish drag selection
        Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
        Pair move_vec = {
            pointer.x - sd->drag_from.x,
            pointer.y - sd->drag_from.y
        };
        Pair area = {MIN(sd->bx, sd->ex), MIN(sd->by, sd->ey)};
        canvas_copy_region(
            dc,
            area,
            (Pair) {MAX(sd->bx, sd->ex) - area.x, MAX(sd->by, sd->ey) - area.y},
            (Pair) {area.x + move_vec.x, area.y + move_vec.y},
            !(event->state & ShiftMask)
        );
    } else if (inp->is_dragging) {
        // select area
        XSetSelectionOwner(dc->dp, XA_PRIMARY, dc->window, CurrentTime);
        trace("clipboard owned");
        return;
    }
    // unselect area
    sd->bx = NIL;
    sd->ey = NIL;
    sd->by = NIL;
    sd->ex = NIL;
    sd->drag_from = (Pair) {NIL, NIL};
    sd->drag_to = (Pair) {NIL, NIL};
    XSetSelectionOwner(dc->dp, XA_PRIMARY, None, CurrentTime);
    trace("clipboard released");
}

void tool_selection_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XMotionEvent const* event
) {
    assert(tc->t == Tool_Selection);
    if (inp->holding_button != XLeftMouseBtn) {
        return;
    }

    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    if (SELECTION_DRAGGING(tc)) {
        tc->d.sel.drag_to = pointer;
    } else if (inp->is_holding) {
        tc->d.sel.ex = CLAMP(pointer.x, 0, dc->cv.width);
        tc->d.sel.ey = CLAMP(pointer.y, 0, dc->cv.height);
    }
}

void tool_pencil_on_press(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XButtonPressedEvent const* event
) {
    assert(tc->t == Tool_Pencil);

    if (event->button != XLeftMouseBtn) {
        return;
    }

    if (event->state & ShiftMask) {
        //
    } else {
        inp->last_processed_pointer = (Pair) {NIL, NIL};
    }
}

void tool_pencil_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XButtonReleasedEvent const* event
) {
    assert(tc->t == Tool_Pencil);

    if (event->button != XLeftMouseBtn || inp->is_dragging) {
        return;
    }

    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    if (event->state & ShiftMask) {
        canvas_line(
            dc,
            point_from_scr_to_cv(dc, inp->last_processed_pointer),
            pointer,
            CURR_COL(tc),
            tc->sdata.line_w
        );
    } else {
        canvas_point(dc, pointer, CURR_COL(tc), tc->sdata.line_w);
    }
    inp->last_processed_pointer = (Pair) {event->x, event->y};
}

void tool_pencil_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XMotionEvent const* event
) {
    assert(tc->t == Tool_Pencil);
    assert(inp->is_holding);

    if (inp->holding_button != XLeftMouseBtn) {
        return;
    }

    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair prev_pointer = (inp->last_processed_pointer.x != NIL)
        ? point_from_scr_to_cv(dc, inp->last_processed_pointer)
        : pointer;
    canvas_line(dc, prev_pointer, pointer, CURR_COL(tc), tc->sdata.line_w);
    inp->last_processed_pointer = (Pair) {event->x, event->y};
}

void tool_pencil_on_move(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XMotionEvent const* event
) {
    assert(tc);
    assert(tc->t == Tool_Pencil);
}

static void flood_fill(XImage* im, u64 targ_col, i32 x, i32 y) {
    assert(im);
    if (x < 0 || y < 0 || x >= im->width || y >= im->height) {
        return;
    }

    static i32 const d_rows[] = {1, 0, 0, -1};
    static i32 const d_cols[] = {0, 1, -1, 0};

    u64 const area_col = XGetPixel(im, x, y);
    if (area_col == targ_col) {
        return;
    }

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
                arrpush(queue_arr, d_curr);
            }
        }
    }

    arrfree(queue_arr);
}

void tool_fill_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XButtonReleasedEvent const* event
) {
    if (inp->holding_button != XLeftMouseBtn) {
        return;
    }
    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);

    flood_fill(dc->cv.im, CURR_COL(tc), pointer.x, pointer.y);
}

void tool_picker_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    struct Input* inp,
    XButtonReleasedEvent const* event
) {
    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);

    if (point_in_rect(
            pointer,
            (Pair) {0, 0},
            (Pair) {(i32)dc->cv.width, (i32)dc->cv.height}
        )) {
        CURR_COL(tc) = XGetPixel(dc->cv.im, pointer.x, pointer.y);
    }
}

Bool history_move(struct Ctx* ctx, Bool forward) {
    struct History** hist_pop =
        forward ? &ctx->hist_prevarr : &ctx->hist_nextarr;
    struct History** hist_save =
        forward ? &ctx->hist_nextarr : &ctx->hist_prevarr;

    if (!arrlenu(*hist_pop)) {
        return False;
    }

    struct History const curr = arrpop(*hist_pop);
    history_push(hist_save, ctx);

    canvas_free(ctx->dc.dp, &ctx->dc.cv);

    // apply history
    ctx->dc.cv = curr.cv;

    return True;
}

Bool history_push(struct History** hist, struct Ctx* ctx) {
    trace("xpaint: history push");

    struct History new_item = {
        .cv = ctx->dc.cv,
    };

    new_item.cv.im =
        XSubImage(ctx->dc.cv.im, 0, 0, ctx->dc.cv.width, ctx->dc.cv.height);

    arrpush(*hist, new_item);

    return True;
}

void historyarr_clear(Display* dp, struct History** histarr) {
    for (u32 i = 0; i < arrlenu(*histarr); ++i) {
        struct History* h = &(*histarr)[i];
        canvas_free(dp, &h->cv);
    }
    arrfree(*histarr);
}

void init_sel_circ_tools(struct SelectionCircle* sc, i32 x, i32 y) {
    static struct Item tools[] = {
        {.on_select = &tool_ctx_set_selection, .hicon = I_Select},
        {.on_select = &tool_ctx_set_pencil, .hicon = I_Pencil},
        {.on_select = &tool_ctx_set_fill, .hicon = I_Fill},
        {.on_select = &tool_ctx_set_picker, .hicon = I_Picker},
    };

    sc->is_active = True;
    sc->x = x;
    sc->y = y;
    sc->item_count = LENGTH(tools);
    sc->items = tools;
}

i32 current_sel_circ_item(struct SelectionCircle const* sc, i32 x, i32 y) {
    i32 const pointer_x_rel = x - sc->x;
    i32 const pointer_y_rel = y - sc->y;
    double const segment_rad = PI * 2 / MAX(1, sc->item_count);
    double const segment_deg = segment_rad / PI * 180;
    double const pointer_r =
        sqrt(pointer_x_rel * pointer_x_rel + pointer_y_rel * pointer_y_rel);

    if (pointer_r > SELECTION_CIRCLE.outer_r_px
        || pointer_r < SELECTION_CIRCLE.inner_r_px) {
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

static Bool ximage_put_checked(XImage* im, u32 x, u32 y, u32 col) {
    if (x >= im->width || y >= im->height) {
        return False;
    }

    XPutPixel(im, x, y, col);
    return True;
}

void canvas_fill(struct DrawCtx* dc, u32 color) {
    assert(dc && dc->cv.im);

    for (i32 i = 0; i < dc->cv.im->width; ++i) {
        for (i32 j = 0; j < dc->cv.im->height; ++j) {
            XPutPixel(dc->cv.im, i, j, color);
        }
    }
}

void canvas_line(
    struct DrawCtx* dc,
    Pair from,
    Pair to,
    u32 color,
    u32 line_w
) {
    assert(dc->cv.im);

    i32 dx = abs(to.x - from.x);
    i32 sx = from.x < to.x ? 1 : -1;
    i32 dy = -abs(to.y - from.y);
    i32 sy = from.y < to.y ? 1 : -1;
    i32 error = dx + dy;

    while (from.x >= 0 && from.y >= 0 && from.x < dc->cv.im->width
           && from.y < dc->cv.im->height) {
        canvas_point(dc, from, color, line_w);
        if (from.x == to.x && from.y == to.y) {
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
    }
}

void canvas_point(struct DrawCtx* dc, Pair c, u32 col, u32 line_w) {
    if (line_w == 1) {
        ximage_put_checked(dc->cv.im, c.x, c.y, col);
    } else {
        canvas_circle(dc, c, line_w / 2, col, True);
    }
}

static void
canvas_circle_helper(XImage* im, i32 x, i32 y, i32 dx, i32 dy, u32 col) {
    ximage_put_checked(im, x + dx, y + dy, col);
    ximage_put_checked(im, x - dx, y + dy, col);
    ximage_put_checked(im, x + dx, y - dy, col);
    ximage_put_checked(im, x - dx, y - dy, col);
    ximage_put_checked(im, x + dy, y + dx, col);
    ximage_put_checked(im, x - dy, y + dx, col);
    ximage_put_checked(im, x + dy, y - dx, col);
    ximage_put_checked(im, x - dy, y - dx, col);
}

void canvas_circle(struct DrawCtx* dc, Pair center, u32 r, u32 col, Bool fill) {
    if (fill) {
        u32 const d = r * 2;
        u32 const l = center.x - r;
        u32 const t = center.y - r;
        for (i32 x = 0; x < d; ++x) {
            for (i32 y = 0; y < d; ++y) {
                if ((x - r) * (x - r) + (y - r) * (y - r) <= r * r) {
                    ximage_put_checked(dc->cv.im, l + x, t + y, col);
                }
            }
        }
    } else {
        // Bresenham's alg
        i32 x = 0;
        i32 y = (i32)r;
        u32 d = 3 - 2 * r;
        canvas_circle_helper(dc->cv.im, center.x, center.y, x, y, col);
        while (y >= x) {
            ++x;
            if (d > 0) {
                --y;
                d += 4 * (x - y) + 10;
            } else {
                d += 4 * x + 6;
            }
            canvas_circle_helper(dc->cv.im, center.x, center.y, x, y, col);
        }
    }
}

void canvas_copy_region(
    struct DrawCtx* dc,
    Pair from,
    Pair dims,
    Pair to,
    Bool clear_source
) {
    i32 const w = dc->cv.im->width;
    i32 const h = dc->cv.im->height;

    u32* region_dyn = (u32*)ecalloc(w * h, sizeof(u32));
    for (i32 get_or_set = 1; get_or_set >= 0; --get_or_set) {
        for (i32 y = 0; y < dims.y; ++y) {
            for (i32 x = 0; x < dims.x; ++x) {
                if (get_or_set) {
                    region_dyn[y * w + x] =
                        XGetPixel(dc->cv.im, from.x + x, from.y + y);
                    if (clear_source) {
                        ximage_put_checked(
                            dc->cv.im,
                            from.x + x,
                            from.y + y,
                            CANVAS.background_argb
                        );
                    }
                } else {
                    ximage_put_checked(
                        dc->cv.im,
                        to.x + x,
                        to.y + y,
                        region_dyn[y * w + x]
                    );
                }
            }
        }
    }
    free(region_dyn);
}

void canvas_fill_rect(struct DrawCtx* dc, Pair c, Pair dims, u32 color) {
    for (u32 x = c.x; x < (c.x + dims.x); ++x) {
        for (u32 y = c.y; y < (c.y + dims.y); ++y) {
            ximage_put_checked(dc->cv.im, x, y, color);
        }
    }
}

static Bool
canvas_load(struct DrawCtx* dc, char const* file_name, u32 transp_argb) {
    XImage* im = read_file_from_path(dc, file_name, 0);
    if (!im) {
        return False;
    }
    canvas_free(dc->dp, &dc->cv);
    dc->cv.im = im;
    dc->cv.type = file_type(file_name);
    dc->cv.width = dc->cv.im->width;
    dc->cv.height = dc->cv.im->height;
    return True;
}

void canvas_free(Display* dp, struct Canvas* cv) {
    if (cv->im) {
        XDestroyImage(cv->im);
        cv->im = NULL;
    }
    cv->width = 0;
    cv->height = 0;
}

void canvas_change_zoom(struct DrawCtx* dc, Pair cursor, i32 delta) {
    double old_zoom = (double)dc->cv.zoom;
    dc->cv.zoom = CLAMP(dc->cv.zoom + delta, 1, CANVAS.max_zoom);
    // keep cursor at same position
    dc->cv.scroll.x += (i32)((dc->cv.scroll.x - cursor.x)
                             * ((double)dc->cv.zoom / old_zoom - 1));
    dc->cv.scroll.y += (i32)((dc->cv.scroll.y - cursor.y)
                             * ((double)dc->cv.zoom / old_zoom - 1));
}

u32 get_statusline_height(struct DrawCtx* dc) {
    return dc->fnt.xfont->ascent + STATUSLINE.padding_bottom;
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

    i32 const outer_r = (i32)SELECTION_CIRCLE.outer_r_px;
    i32 const inner_r = (i32)SELECTION_CIRCLE.inner_r_px;

    XSetLineAttributes(
        dc->dp,
        dc->screen_gc,
        SELECTION_CIRCLE.line_w,
        SELECTION_CIRCLE.line_style,
        SELECTION_CIRCLE.cap_style,
        SELECTION_CIRCLE.join_style
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
        double const segment_rad = PI * 2 / MAX(1, sc->item_count);
        double const segment_deg = segment_rad / PI * 180;

        // item images
        for (u32 item = 0; item < sc->item_count; ++item) {
            XImage* image = images[sc->items[item].hicon];
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
        i32 const current_item =
            current_sel_circ_item(sc, pointer_x, pointer_y);
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

        if (sc->item_count >= 2) {  // segment lines
            XSetForeground(dc->dp, dc->screen_gc, COL_FG(dc, SchmNorm));
            for (u32 line_num = 0; line_num < sc->item_count; ++line_num) {
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

static void draw_string(
    struct DrawCtx* dc,
    char const* str,
    Pair c,
    enum Schm sc,
    Bool invert
) {
    XftDraw* d =
        XftDrawCreate(dc->dp, dc->back_buffer, dc->vinfo.visual, dc->colmap);
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

static void
draw_int(struct DrawCtx* dc, i32 i, Pair c, enum Schm sc, Bool invert) {
    char* msg = str_new("%d", i);
    draw_string(dc, msg, c, sc, invert);
    str_free(&msg);
}

// XXX always opaque
static int fill_rect(struct DrawCtx* dc, Pair p, Pair dim, u32 col) {
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

static int draw_rect(
    struct DrawCtx* dc,
    Pair p,
    Pair dim,
    u32 col,
    u32 line_w,
    i32 line_st,
    i32 cap_st,
    i32 join_st
) {
    XSetForeground(dc->dp, dc->screen_gc, col);
    XSetLineAttributes(dc->dp, dc->screen_gc, line_w, line_st, cap_st, join_st);
    return XDrawRectangle(
        dc->dp,
        dc->back_buffer,
        dc->screen_gc,
        p.x,
        p.y,
        dim.x,
        dim.y
    );
}

static u32
get_string_width(struct DrawCtx const* dc, char const* str, u32 len) {
    XGlyphInfo ext;
    XftTextExtentsUtf8(dc->dp, dc->fnt.xfont, (XftChar8*)str, (i32)len, &ext);
    return ext.xOff;
}

static u32 get_int_width(struct DrawCtx const* dc, char const* format, u32 i) {
    static u32 const MAX_BUF = 50;
    char buf[MAX_BUF];
    snprintf(buf, MAX_BUF, format, i);
    return get_string_width(dc, buf, strlen(buf));
}

void update_screen(struct Ctx* ctx) {
    struct DrawCtx* dc = &ctx->dc;
    /* draw canvas */ {
        fill_rect(
            dc,
            (Pair) {0, 0},
            (Pair) {(i32)dc->width, (i32)dc->height},
            WINDOW.background_argb
        );
        /* put scaled image */ {
            //  https://stackoverflow.com/a/66896097
            u32 pb_width = dc->cv.width * dc->cv.zoom;
            u32 pb_height = dc->cv.height * dc->cv.zoom;
            if (dc->cache.pm == 0 || dc->cache.pm_w != pb_width
                || dc->cache.pm_h != pb_height) {
                if (dc->cache.pm != 0) {
                    XFreePixmap(dc->dp, dc->cache.pm);
                }
                dc->cache.pm = XCreatePixmap(
                    dc->dp,
                    dc->window,
                    pb_width,
                    pb_height,
                    dc->vinfo.depth
                );
                dc->cache.pm_w = pb_width;
                dc->cache.pm_h = pb_height;
            }
            XPutImage(
                dc->dp,
                dc->cache.pm,
                dc->screen_gc,
                dc->cv.im,
                0,
                0,
                0,
                0,
                pb_width,
                pb_height
            );

            Picture src_pict = XRenderCreatePicture(
                dc->dp,
                dc->cache.pm,
                dc->xrnd_pic_format,
                0,
                &(XRenderPictureAttributes) {.subwindow_mode = IncludeInferiors}
            );
            Picture dst_pict = XRenderCreatePicture(
                dc->dp,
                dc->back_buffer,
                dc->xrnd_pic_format,
                0,
                &(XRenderPictureAttributes) {.subwindow_mode = IncludeInferiors}
            );

            double const z = 1.0 / dc->cv.zoom;
            XRenderSetPictureTransform(
                dc->dp,
                src_pict,
                &(XTransform) {{
                    {XDoubleToFixed(z), XDoubleToFixed(0), XDoubleToFixed(0)},
                    {XDoubleToFixed(0), XDoubleToFixed(z), XDoubleToFixed(0)},
                    {XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1)},
                }}
            );

            // clang-format off
            XRenderComposite(
                dc->dp, PictOpSrc,
                src_pict, 0,
                dst_pict,
                0, 0,
                0, 0,
                dc->cv.scroll.x, dc->cv.scroll.y,
                pb_width, pb_height
            );
            // clang-format on

            XRenderFreePicture(dc->dp, src_pict);
            XRenderFreePicture(dc->dp, dst_pict);
        }
    }
    /* current selection */ {
        if (HAS_SELECTION(&CURR_TC(ctx))) {
            struct SelectionData sd = CURR_TC(ctx).d.sel;
            Pair p = {MIN(sd.bx, sd.ex), MIN(sd.by, sd.ey)};
            Pair dim = {MAX(sd.bx, sd.ex) - p.x, MAX(sd.by, sd.ey) - p.y};
            draw_rect(
                dc,
                point_from_cv_to_scr(dc, p),
                point_from_cv_to_scr_no_move(dc, dim),
                0x00000,
                SELECTION_TOOL.line_w,
                SELECTION_TOOL.line_style,
                SELECTION_TOOL.cap_style,
                SELECTION_TOOL.join_style
            );
            if (SELECTION_DRAGGING(&CURR_TC(ctx))) {
                i32 dx = sd.drag_to.x - sd.drag_from.x;
                i32 dy = sd.drag_to.y - sd.drag_from.y;
                draw_rect(
                    dc,
                    point_from_cv_to_scr_xy(dc, p.x + dx, p.y + dy),
                    point_from_cv_to_scr_no_move(dc, dim),
                    SELECTION_TOOL.drag_argb,
                    SELECTION_TOOL.line_w,
                    SELECTION_TOOL.line_style,
                    SELECTION_TOOL.cap_style,
                    SELECTION_TOOL.join_style
                );
            }
        }
    }

    update_statusline(ctx);  // backbuffer swaped here
}

void update_statusline(struct Ctx* ctx) {
    struct DrawCtx* dc = &ctx->dc;
    struct ToolCtx* tc = &CURR_TC(ctx);
    u32 const statusline_h = get_statusline_height(dc);
    fill_rect(
        dc,
        (Pair) {0, (i32)(dc->height - statusline_h)},
        (Pair) {(i32)dc->width, (i32)statusline_h},
        COL_BG(&ctx->dc, SchmNorm)
    );
    if (ctx->input.t == InputT_Console) {
        char const* command = ctx->input.d.cl.cmdarr;
        usize const command_len = arrlen(command);
        // extra 2 for prompt and terminator
        char* cl_str_dyn = ecalloc(2 + command_len, sizeof(char));
        cl_str_dyn[0] = ':';
        cl_str_dyn[command_len + 1] = '\0';
        memcpy(cl_str_dyn + 1, command, command_len);
        i32 const user_cmd_w =
            (i32)get_string_width(&ctx->dc, cl_str_dyn, command_len + 1);
        i32 const cmd_y = (i32)(ctx->dc.height - STATUSLINE.padding_bottom);
        draw_string(&ctx->dc, cl_str_dyn, (Pair) {0, cmd_y}, SchmNorm, False);
        if (ctx->input.d.cl.compls_arr) {
            draw_string(
                &ctx->dc,
                ctx->input.d.cl.compls_arr[ctx->input.d.cl.compls_curr],
                (Pair) {user_cmd_w, cmd_y},
                SchmFocus,
                False
            );
        }
        str_free(&cl_str_dyn);
    } else {
        static u32 const gap = 5;
        static u32 const small_gap = gap / 2;
        // widths of captions
        // XXX uses 'F' as average character
        u32 const col_count_w = get_string_width(dc, "/", 1)
            + get_int_width(dc, "%d", MAX_COLORS) * 2 + gap;
        u32 tcs_w = 0;
        /* tcs_w */ {
            for (i32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                tcs_w += get_int_width(dc, "%d", tc_name) + small_gap;
            }
            tcs_w += gap;
        }
        u32 const col_name_w = get_string_width(dc, "#FFFFFF", 7) + gap;
        // FIXME how many symbols?
        u32 const input_state_w =
            get_string_width(dc, "FFFFFFFFFFFFFF", 14) + gap;
        u32 const tool_name_w =
            get_string_width(dc, "FFFFFFFFFFFFFF", 14) + gap;
        // left **bottom** corners of captions
        Pair const tcs_c = {0, (i32)(dc->height - STATUSLINE.padding_bottom)};
        Pair const input_state_c = {(i32)(tcs_c.x + tcs_w), tcs_c.y};
        Pair const tool_name_c = {
            (i32)(input_state_c.x + input_state_w),
            input_state_c.y
        };
        Pair const line_w_c = {
            (i32)(tool_name_c.x + tool_name_w),
            tool_name_c.y
        };
        Pair const col_count_c = {(i32)(dc->width - col_count_w), line_w_c.y};
        Pair const col_c = {(i32)(col_count_c.x - col_name_w), col_count_c.y};
        // colored rectangle
        static u32 const col_rect_w = 30;
        static u32 const col_value_size = 1 + 6;

        XSetBackground(dc->dp, dc->screen_gc, COL_BG(&ctx->dc, SchmNorm));
        XSetForeground(dc->dp, dc->screen_gc, COL_FG(&ctx->dc, SchmNorm));
        /* tc */ {
            i32 x = tcs_c.x;
            for (i32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                draw_int(
                    dc,
                    tc_name,
                    (Pair) {x, tcs_c.y},
                    ctx->curr_tc == (tc_name - 1) ? SchmFocus : SchmNorm,
                    False
                );
                x += (i32)(get_int_width(dc, "%d", tc_name) + small_gap);
            }
        }
        /* input state */ {
            draw_string(
                dc,
                ctx->input.t == InputT_Interact      ? "intearct"
                    : ctx->input.t == InputT_Color   ? "color"
                    : ctx->input.t == InputT_Console ? "console"
                                                     : "unknown",
                input_state_c,
                SchmNorm,
                False
            );
        }
        draw_string(
            dc,
            tc->tool_name_dyn ? tc->tool_name_dyn : "N/A",
            tool_name_c,
            SchmNorm,
            False
        );
        draw_int(dc, (i32)tc->sdata.line_w, line_w_c, SchmNorm, False);
        /* color */ {
            char col_value[col_value_size + 1];
            sprintf(col_value, "#%06X", CURR_COL(tc) & 0xFFFFFF);
            draw_string(dc, col_value, col_c, SchmNorm, False);
            /* color count */ {
                // FIXME how it possible
                char col_count[digit_count(MAX_COLORS) * 2 + 1 + 1];
                sprintf(
                    col_count,
                    "%d/%td",
                    tc->sdata.curr_col + 1,
                    arrlen(tc->sdata.col_argbarr)
                );
                draw_string(dc, col_count, col_count_c, SchmNorm, False);
            }
            if (ctx->input.t == InputT_Color) {
                static u32 const hash_w = 1;
                u32 const curr_dig = ctx->input.d.col.current_digit;
                char const col_digit_value[] =
                    {[0] = col_value[curr_dig + hash_w], [1] = '\0'};
                draw_string(
                    dc,
                    col_digit_value,
                    (Pair
                    ) {col_c.x
                           + (i32
                           )get_string_width(dc, col_value, curr_dig + hash_w),
                       col_c.y},
                    SchmFocus,
                    False
                );
            }
            fill_rect(
                dc,
                (Pair
                ) {(i32)(dc->width - col_name_w - col_rect_w - col_count_w),
                   (i32)(dc->height - statusline_h)},
                (Pair) {(i32)col_rect_w, (i32)statusline_h},
                CURR_COL(tc)
            );
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
}

// FIXME DRY
void show_message(struct Ctx* ctx, char const* msg) {
    u32 const statusline_h =
        ctx->dc.fnt.xfont->ascent + STATUSLINE.padding_bottom;
    fill_rect(
        &ctx->dc,
        (Pair) {0, (i32)(ctx->dc.height - statusline_h)},
        (Pair) {(i32)ctx->dc.width, (i32)statusline_h},
        COL_BG(&ctx->dc, SchmNorm)
    );
    draw_string(
        &ctx->dc,
        msg,
        (Pair) {0, (i32)(ctx->dc.height - STATUSLINE.padding_bottom)},
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

void show_message_va(struct Ctx* ctx, char const* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    char* msg_dyn = str_new_va(fmt, ap);
    show_message(ctx, msg_dyn);
    str_free(&msg_dyn);
    va_end(ap);
}

void canvas_resize(struct DrawCtx* dc, i32 new_width, i32 new_height) {
    if (new_width <= 0 || new_height <= 0) {
        trace("resize_canvas: invalid canvas size");
        return;
    }
    u32 old_width = dc->cv.width;
    u32 old_height = dc->cv.height;
    dc->cv.width = new_width;
    dc->cv.height = new_height;

    // FIXME can fill color be changed?
    XImage* new_cv_im = XSubImage(dc->cv.im, 0, 0, new_width, new_height);
    XDestroyImage(dc->cv.im);
    dc->cv.im = new_cv_im;

    // fill new area if needed
    if (old_width < new_width) {
        canvas_fill_rect(
            dc,
            (Pair) {(i32)old_width, 0},
            (Pair) {(i32)(new_width - old_width), new_height},
            CANVAS.background_argb
        );
    }
    if (old_height < new_height) {
        canvas_fill_rect(
            dc,
            (Pair) {0, (i32)old_height},
            (Pair) {new_width, (i32)(new_height - old_height)},
            CANVAS.background_argb
        );
    }
}

void free_sel_circ(struct SelectionCircle* sel_circ) {
    sel_circ->is_active = False;
}

void run(struct Ctx* ctx) {
    static Bool (*handlers[])(struct Ctx*, XEvent*) = {
        [ButtonPress] = &button_press_hdlr,
        [ButtonRelease] = &button_release_hdlr,
        [DestroyNotify] = &destroy_notify_hdlr,
        [Expose] = &expose_hdlr,
        [KeyPress] = &key_press_hdlr,
        [MappingNotify] = &mapping_notify_hdlr,
        [MotionNotify] = &motion_notify_hdlr,
        [ConfigureNotify] = &configure_notify_hdlr,
        [SelectionRequest] = &selection_request_hdlr,
        [SelectionNotify] = &selection_notify_hdlr,
        [ClientMessage] = &client_message_hdlr,
    };

    Bool running = True;
    XEvent event;

    XSync(ctx->dc.dp, False);
    while (running && !XNextEvent(ctx->dc.dp, &event)) {
        if (XFilterEvent(&event, ctx->dc.window)) {
            continue;
        }
        if (handlers[event.type]) {
            running = handlers[event.type](ctx, &event);
        }
    }
}

struct Ctx ctx_init(Display* dp) {
    return (struct Ctx) {
        .dc =
            (struct DrawCtx) {
                .dp = dp,
                .width = CANVAS.default_width,
                .height = CANVAS.default_height,
                .cv =
                    (struct Canvas) {
                        .width = NIL,
                        .height = NIL,
                        .im = NULL,
                        .type = IMT_Png,  // save as png by default
                        .zoom = 1,
                        .scroll = {0, 0},
                    },
                .cache = (struct Cache) {.pm = 0},
            },
        .input =
            (struct Input) {
                .t = InputT_Interact,
                .last_processed_pointer = {NIL, NIL},
            },
        .sel_buf.im = NULL,
        .tcarr = NULL,
        .curr_tc = 0,
        .hist_nextarr = NULL,
        .hist_prevarr = NULL,
    };
}

void setup(Display* dp, struct Ctx* ctx) {
    assert(dp);
    assert(ctx);

    /* init arrays */ {
        for (i32 i = 0; i < TCS_NUM; ++i) {
            struct ToolCtx tc = {
                .sdata.col_argbarr = NULL,
                .sdata.curr_col = 0,
                .sdata.line_w = TOOLS.default_line_w,
            };
            arrpush(ctx->tcarr, tc);
            arrpush(ctx->tcarr[i].sdata.col_argbarr, 0xFF000000);
            arrpush(ctx->tcarr[i].sdata.col_argbarr, 0xFFFFFFFF);
        }
    }

    /* atoms */ {
        atoms[A_Clipboard] = XInternAtom(dp, "CLIPBOARD", False);
        atoms[A_Targets] = XInternAtom(dp, "TARGETS", False);
        atoms[A_Utf8string] = XInternAtom(dp, "UTF8_STRING", False);
        atoms[A_ImagePng] = XInternAtom(dp, "image/png", False);
    }

    /* xrender */ {
        ctx->dc.xrnd_pic_format =
            XRenderFindStandardFormat(ctx->dc.dp, PictStandardARGB32);
        assert(ctx->dc.xrnd_pic_format);
    }

    i32 screen = DefaultScreen(dp);
    Window root = DefaultRootWindow(dp);

    i32 result = XMatchVisualInfo(dp, screen, 32, TrueColor, &ctx->dc.vinfo);
    assert(result != 0);

    ctx->dc.colmap = XCreateColormap(dp, root, ctx->dc.vinfo.visual, AllocNone);

    /* create window */
    ctx->dc.window = XCreateWindow(
        dp,
        root,
        0,
        0,
        ctx->dc.width,
        ctx->dc.height,
        0,  // border width
        ctx->dc.vinfo.depth,
        InputOutput,
        ctx->dc.vinfo.visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWEventMask,
        &(XSetWindowAttributes
        ) {.colormap = ctx->dc.colmap,
           .border_pixel = 0,
           .background_pixel = 0xFFFF00FF,
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

    /* X input method setup */ {
        // from https://gist.github.com/baines/5a49f1334281b2685af5dcae81a6fa8a
        XSetLocaleModifiers("");

        ctx->dc.xim = XOpenIM(dp, 0, 0, 0);
        if (!ctx->dc.xim) {
            // fallback to internal input method
            XSetLocaleModifiers("@im=none");
            ctx->dc.xim = XOpenIM(dp, 0, 0, 0);
        }

        ctx->dc.xic = XCreateIC(
            ctx->dc.xim,
            XNInputStyle,
            XIMPreeditNothing | XIMStatusNothing,
            XNClientWindow,
            ctx->dc.window,
            XNFocusWindow,
            ctx->dc.window,
            NULL
        );
        XSetICFocus(ctx->dc.xic);
    }

    ctx->dc.back_buffer = XdbeAllocateBackBufferName(dp, ctx->dc.window, 0);

    /* turn on protocol support */ {
        Atom wm_delete_window = XInternAtom(dp, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dp, ctx->dc.window, &wm_delete_window, 1);
    }

    if (!fnt_set(&ctx->dc, FONT_NAME)) {
        die("failed to load default font: %s", FONT_NAME);
    }

    /* schemes */ {
        ctx->dc.schemes_dyn = ecalloc(SchmLast, sizeof(ctx->dc.schemes_dyn[0]));
        for (i32 i = 0; i < SchmLast; ++i) {
            for (i32 j = 0; j < 2; ++j) {
                if (!XftColorAllocValue(
                        dp,
                        ctx->dc.vinfo.visual,
                        ctx->dc.colmap,
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
            u8 const* data = i == I_Select ? pic_tool_select_data
                : i == I_Pencil            ? pic_tool_pencil_data
                : i == I_Fill              ? pic_tool_fill_data
                : i == I_Picker            ? pic_tool_picker_data
                                           : pic_unknown_data;
            u32 const len = i == I_Select ? RES_SZ_TOOL_SELECT
                : i == I_Pencil           ? RES_SZ_TOOL_PENCIL
                : i == I_Fill             ? RES_SZ_TOOL_FILL
                : i == I_Picker           ? RES_SZ_TOOL_PICKER
                                          : RES_SZ_UNKNOWN;
            images[i] = read_file_from_memory(
                &ctx->dc,
                data,
                len,
                COL_BG(&ctx->dc, SchmNorm)
            );
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
        if (ctx->finp.path_dyn) {
            if (!canvas_load(&ctx->dc, ctx->finp.path_dyn, 0)) {
                die("xpaint: failed to read input file");
            }
        } else {
            ctx->dc.cv.width = ctx->dc.cv.width != NIL ? ctx->dc.cv.width
                                                       : CANVAS.default_width;
            ctx->dc.cv.height = ctx->dc.cv.height != NIL
                ? ctx->dc.cv.height
                : CANVAS.default_height;
            Pixmap data = XCreatePixmap(
                dp,
                ctx->dc.window,
                ctx->dc.cv.width,
                ctx->dc.cv.height,
                ctx->dc.vinfo.depth
            );
            ctx->dc.cv.im = XGetImage(
                dp,
                data,
                0,
                0,
                ctx->dc.cv.width,
                ctx->dc.cv.height,
                AllPlanes,
                ZPixmap
            );
            XFreePixmap(dp, data);
            // initial canvas color
            canvas_fill(&ctx->dc, CANVAS.background_argb);
        }

        ctx->dc.width = ctx->dc.cv.width;
        ctx->dc.height = ctx->dc.cv.height + get_statusline_height(&ctx->dc);
        XResizeWindow(dp, ctx->dc.window, ctx->dc.width, ctx->dc.height);
    }

    for (i32 i = 0; i < TCS_NUM; ++i) {
        tool_ctx_set_pencil(&ctx->tcarr[i]);
    }
    history_push(&ctx->hist_prevarr, ctx);

    /* show up window */
    XMapRaised(dp, ctx->dc.window);
}

Bool button_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    if (e->button == XLeftMouseBtn) {
        // next history invalidated after user action
        historyarr_clear(ctx->dc.dp, &ctx->hist_nextarr);
        history_push(&ctx->hist_prevarr, ctx);
    }
    if (CURR_TC(ctx).on_press) {
        CURR_TC(ctx).on_press(&ctx->dc, &CURR_TC(ctx), &ctx->input, e);
        update_screen(ctx);
    }
    if (e->button == XRightMouseBtn) {
        init_sel_circ_tools(&ctx->sc, e->x, e->y);
        draw_selection_circle(&ctx->dc, &ctx->sc, NIL, NIL);
    }

    ctx->input.holding_button = e->button;
    ctx->input.is_holding = True;

    return True;
}

Bool button_release_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    if (e->button == XRightMouseBtn) {
        i32 const selected_item = current_sel_circ_item(&ctx->sc, e->x, e->y);
        if (selected_item != NIL && ctx->sc.items[selected_item].on_select) {
            ctx->sc.items[selected_item].on_select(&CURR_TC(ctx));
        }
        free_sel_circ(&ctx->sc);
        update_screen(ctx);
        ctx->input.is_holding = False;
        ctx->input.is_dragging = False;
        return True;  // something selected do nothing else
    }

    if (e->button == XMouseScrollDown || e->button == XMouseScrollUp) {
        i32 sign = e->button == XMouseScrollUp ? 1 : -1;
        if (e->state & ControlMask) {
            canvas_change_zoom(&ctx->dc, ctx->input.prev_c, sign);
        } else if (e->state & ShiftMask) {
            ctx->dc.cv.scroll.x -= sign * 10;
        } else {
            ctx->dc.cv.scroll.y += sign * 10;
        }
        update_screen(ctx);
    }

    if (CURR_TC(ctx).on_release) {
        CURR_TC(ctx).on_release(&ctx->dc, &CURR_TC(ctx), &ctx->input, e);
        update_screen(ctx);
    }

    ctx->input.is_holding = False;
    ctx->input.is_dragging = False;

    return True;
}

Bool destroy_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    return True;
}

Bool expose_hdlr(struct Ctx* ctx, XEvent* event) {
    update_screen(ctx);
    return True;
}

#define HANDLE_KEY_BEGIN(p_xkeypressedevent) \
    { \
        KeySym const handle_key_inp_key_sym = \
            XLookupKeysym(&(p_xkeypressedevent), 0); \
        XKeyPressedEvent const handle_key_inp_event = p_xkeypressedevent;
#define HANDLE_KEY_END() }
#define HANDLE_KEY_CASE_MASK(p_mask, p_key) \
    if (handle_key_inp_event.state & (p_mask) \
        && handle_key_inp_key_sym == (p_key))
#define HANDLE_KEY_CASE_MASK_NOT(p_mask, p_key) \
    if (!(handle_key_inp_event.state & (p_mask)) \
        && handle_key_inp_key_sym == (p_key))
#define HANDLE_KEY_CASE(p_key) if (handle_key_inp_key_sym == (p_key))

static void to_next_input_digit(struct Input* input, Bool is_increment) {
    assert(input->t == InputT_Color);

    if (input->d.col.current_digit == 0 && !is_increment) {
        input->d.col.current_digit = 5;
    } else if (input->d.col.current_digit == 5 && is_increment) {
        input->d.col.current_digit = 0;
    } else {
        input->d.col.current_digit += is_increment ? 1 : -1;
    }
}

Bool key_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XKeyPressedEvent e = event->xkey;
    if (e.type == KeyRelease) {
        return True;
    }

    Status lookup_status;
    KeySym key_sym = NoSymbol;
    char lookup_buf[32] = {0};
    i32 const text_len = Xutf8LookupString(
        ctx->dc.xic,
        &e,
        lookup_buf,
        sizeof(lookup_buf) - 1,
        &key_sym,
        &lookup_status
    );

    if (lookup_status == XBufferOverflow) {
        trace("xpaint: input buffer overflow");
    }

    HANDLE_KEY_BEGIN(e)
    switch (ctx->input.t) {
        case InputT_Interact: {
            HANDLE_KEY_CASE_MASK(ControlMask, XK_z) {
                if (!history_move(ctx, !(e.state & ShiftMask))) {
                    trace("xpaint: can't undo/revert history");
                }
                update_screen(ctx);
            }
            HANDLE_KEY_CASE_MASK(ControlMask, XK_c) {
                if (HAS_SELECTION(&CURR_TC(ctx))) {
                    XSetSelectionOwner(
                        ctx->dc.dp,
                        atoms[A_Clipboard],
                        ctx->dc.window,
                        CurrentTime
                    );
                    i32 x = MIN(CURR_TC(ctx).d.sel.bx, CURR_TC(ctx).d.sel.ex);
                    i32 y = MIN(CURR_TC(ctx).d.sel.by, CURR_TC(ctx).d.sel.ey);
                    u32 width =
                        MAX(CURR_TC(ctx).d.sel.ex, CURR_TC(ctx).d.sel.bx) - x;
                    u32 height =
                        MAX(CURR_TC(ctx).d.sel.ey, CURR_TC(ctx).d.sel.by) - y;
                    if (ctx->sel_buf.im != NULL) {
                        XDestroyImage(ctx->sel_buf.im);
                    }
                    ctx->sel_buf.im =
                        XSubImage(ctx->dc.cv.im, x, y, width, height);
                    assert(ctx->sel_buf.im != NULL);
                    assert(
                        ctx->sel_buf.im->width == width
                        && ctx->sel_buf.im->height == height
                    );
                    if (ctx->sel_buf.im->red_mask == 0
                        && ctx->sel_buf.im->green_mask == 0
                        && ctx->sel_buf.im->blue_mask == 0) {
                        puts("ximage: XGetImage returned empty masks");
                        ctx->sel_buf.im->red_mask = 0xFF0000;
                        ctx->sel_buf.im->green_mask = 0xFF00;
                        ctx->sel_buf.im->blue_mask = 0xFF;
                    }
                    if (is_verbose_output) {
                        u32 const image_size = ctx->sel_buf.im->bits_per_pixel
                            * ctx->sel_buf.im->height;
                        trace(
                            "\nsize: %d\nwidth: %d\nheight: %d\nbpp: %d\nbbo: %d\n"
                            "format: %d\nred: %lX\nblue: %lX\ngreen %lX\n",
                            image_size,
                            ctx->sel_buf.im->width,
                            ctx->sel_buf.im->height,
                            ctx->sel_buf.im->bits_per_pixel,
                            ctx->sel_buf.im->bitmap_bit_order,
                            ctx->sel_buf.im->format,
                            ctx->sel_buf.im->red_mask,
                            ctx->sel_buf.im->blue_mask,
                            ctx->sel_buf.im->green_mask
                        );
                    }
                } else {
                    trace("^c without selection");
                }
            }
            HANDLE_KEY_CASE_MASK_NOT(ControlMask, XK_c) {
                input_state_set(&ctx->input, InputT_Color);
                update_statusline(ctx);
            }
            if (BETWEEN(key_sym, XK_1, XK_9)) {
                u32 val = key_sym - XK_1;
                if (val < TCS_NUM) {
                    ctx->curr_tc = val;
                    update_statusline(ctx);
                }
            }
            if (BETWEEN(key_sym, XK_Left, XK_Down) && e.state & ControlMask) {
                u32 const value = e.state & ShiftMask ? 25 : 5;
                canvas_resize(
                    &ctx->dc,
                    (i32)(ctx->dc.cv.width
                          + (key_sym == XK_Left        ? -value
                                 : key_sym == XK_Right ? value
                                                       : 0)),
                    (i32)(ctx->dc.cv.height
                          + (key_sym == XK_Down     ? -value
                                 : key_sym == XK_Up ? value
                                                    : 0))
                );
                update_screen(ctx);
            }
            HANDLE_KEY_CASE_MASK(ControlMask, XK_equal) {
                canvas_change_zoom(&ctx->dc, ctx->input.prev_c, 1);
                update_screen(ctx);
            }
            HANDLE_KEY_CASE_MASK(ControlMask, XK_minus) {
                canvas_change_zoom(&ctx->dc, ctx->input.prev_c, -1);
                update_screen(ctx);
            }
            // XK_colon not work for some reason
            HANDLE_KEY_CASE_MASK(ShiftMask, XK_semicolon) {
                input_state_set(&ctx->input, InputT_Console);
                update_statusline(ctx);
            }
        } break;

        case InputT_Color: {
            HANDLE_KEY_CASE_MASK(ControlMask, XK_Up) {
                u32 const len = arrlen(CURR_TC(ctx).sdata.col_argbarr);
                if (len != MAX_COLORS) {
                    CURR_TC(ctx).sdata.curr_col = len;
                    arrpush(CURR_TC(ctx).sdata.col_argbarr, 0xFF000000);
                    update_statusline(ctx);
                }
            }
            HANDLE_KEY_CASE(XK_Right) {
                to_next_input_digit(&ctx->input, True);
                update_statusline(ctx);
            }
            HANDLE_KEY_CASE(XK_Left) {
                to_next_input_digit(&ctx->input, False);
                update_statusline(ctx);
            }
            // change selected color digit with pressed key
            if (strlen(lookup_buf) == 1) {
                u32 val = lookup_buf[0]
                    - (BETWEEN(lookup_buf[0], '0', '9')       ? '0'
                           : BETWEEN(lookup_buf[0], 'a', 'f') ? ('a' - 10)
                           : BETWEEN(lookup_buf[0], 'A', 'F') ? ('A' - 10)
                                                              : 0);
                if (val != lookup_buf[0]) {  // if assigned
                    // selected digit counts from left to
                    // right except alpha (aarrggbb <=> --012345)
                    u32 shift = (5 - ctx->input.d.col.current_digit) * 4;
                    CURR_COL(&CURR_TC(ctx)) &= ~(0xF << shift);  // clear
                    CURR_COL(&CURR_TC(ctx)) |= val << shift;  // set
                    to_next_input_digit(&ctx->input, True);
                    update_statusline(ctx);
                }
            }
        } break;

        case InputT_Console: {
            struct InputConsoleData* cl = &ctx->input.d.cl;
            if (key_sym == XK_Return) {
                if (cl->compls_arr) {  // apply completion
                    char* compl = cl->compls_arr[cl->compls_curr];
                    while (*compl ) {
                        arrpush(cl->cmdarr, *compl );
                        ++compl ;
                    }
                    cl_compls_free(cl);
                    update_statusline(ctx);
                } else {  // apply command
                    char* cmd_dyn = cl_cmd_get_str_dyn(cl);
                    input_state_set(&ctx->input, InputT_Interact);
                    ClCPrsResult res = cl_cmd_parse(ctx, cmd_dyn);
                    str_free(&cmd_dyn);
                    Bool is_exit = False;
                    switch (res.t) {
                        case ClCPrs_Ok: {
                            struct ClCommand* cmd = &res.d.ok;
                            ClCPrcResult res = cl_cmd_process(ctx, cmd);
                            update_screen(ctx);
                            if (res.bit_status & ClCPrc_Msg) {
                                show_message(ctx, res.msg_dyn);
                                str_free(&res.msg_dyn);  // XXX member free
                            }
                            is_exit = (Bool)(res.bit_status & ClCPrc_Exit);
                        } break;
                        case ClCPrs_ENoArg: {
                            show_message(ctx, "no command");
                        } break;
                        case ClCPrs_EInvArg: {
                            show_message_va(
                                ctx,
                                "invalid arg '%s'",
                                res.d.invarg
                            );
                        } break;
                        case ClCPrs_ENoSubArg: {
                            show_message_va(
                                ctx,
                                "provide value to '%s' cmd",
                                res.d.nosubarg.arg_dyn
                            );
                        } break;
                        case ClCPrs_EInvSubArg: {
                            show_message_va(
                                ctx,
                                "invalid arg '%s' provided to '%s' cmd",
                                res.d.invsubarg.inv_val_dyn,
                                res.d.invsubarg.arg_dyn
                            );
                        } break;
                    }

                    cl_cmd_parse_res_free(&res);
                    if (is_exit) {
                        return False;
                    }
                }
            } else if (key_sym == XK_Tab) {
                if (!cl->compls_valid) {
                    cl_compls_update(cl);
                } else {
                    usize max = arrlen(cl->compls_arr);
                    if (max) {
                        cl->compls_curr = (cl->compls_curr + 1) % max;
                    }
                }
                update_statusline(ctx);
            } else if (key_sym == XK_BackSpace) {
                cl_pop(cl);
                update_statusline(ctx);
            } else if (key_sym != XK_Escape) {
                if ((lookup_status == XLookupBoth
                     || lookup_status == XLookupChars)
                    && !(iscntrl((u32)*lookup_buf))) {
                    for (i32 i = 0; i < text_len; ++i) {
                        cl_push(cl, (char)(lookup_buf[i] & 0xFF));
                    }
                    update_statusline(ctx);
                }
            }
        } break;

        default: assert(!"unknown input state");
    }
    // any state except console input
    if (ctx->input.t != InputT_Console) {
        HANDLE_KEY_CASE(XK_q) {
            return False;
        }
        if ((key_sym == XK_Up || key_sym == XK_Down)
            && !(e.state & ControlMask)) {
            u32 col_num = arrlen(CURR_TC(ctx).sdata.col_argbarr);
            assert(col_num != 0);
            CURR_TC(ctx).sdata.curr_col =
                (CURR_TC(ctx).sdata.curr_col + (key_sym == XK_Up ? 1 : -1))
                % col_num;
            update_statusline(ctx);
        }
        HANDLE_KEY_CASE_MASK(ControlMask, XK_s) {  // save to current file
            if (save_file(&ctx->dc, ctx->dc.cv.type, ctx->fout.path_dyn)) {
                trace("xpaint: file saved");
            } else {
                trace("xpaint: failed to save image");
            }
        }
    }
    // independent
    HANDLE_KEY_CASE(XK_Escape) {
        input_state_set(&ctx->input, InputT_Interact);
        update_statusline(ctx);
    }
    HANDLE_KEY_END()

    return True;
}

Bool mapping_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XRefreshKeyboardMapping(&event->xmapping);
    return True;
}

Bool motion_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XMotionEvent* e = (XMotionEvent*)event;

    if (ctx->input.is_holding) {
        ctx->input.is_dragging = True;
        if (CURR_TC(ctx).on_drag) {
            struct timeval current_time;
            gettimeofday(&current_time, 0x0);
            if (current_time.tv_usec - ctx->input.last_proc_drag_ev_us
                >= DRAG_PERIOD_US) {
                CURR_TC(ctx).on_drag(&ctx->dc, &CURR_TC(ctx), &ctx->input, e);
                ctx->input.last_proc_drag_ev_us = current_time.tv_usec;
                update_screen(ctx);
            }
        }
        if (ctx->input.holding_button == XMiddleMouseBtn) {
            ctx->dc.cv.scroll.x += e->x - ctx->input.prev_c.x;
            ctx->dc.cv.scroll.y += e->y - ctx->input.prev_c.y;
            update_screen(ctx);
        }
    } else {
        if (CURR_TC(ctx).on_move) {
            CURR_TC(ctx).on_move(&ctx->dc, &CURR_TC(ctx), &ctx->input, e);
            ctx->input.last_proc_drag_ev_us = 0;
            update_screen(ctx);
        }
    }

    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    ctx->input.prev_c.x = e->x;
    ctx->input.prev_c.y = e->y;

    return True;
}

Bool configure_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    ctx->dc.width = event->xconfigure.width;
    ctx->dc.height = event->xconfigure.height;

    // backbuffer resizes automatically

    return True;
}

Bool selection_request_hdlr(struct Ctx* ctx, XEvent* event) {
    XSelectionRequestEvent request = event->xselectionrequest;

    if (XGetSelectionOwner(ctx->dc.dp, atoms[A_Clipboard]) == ctx->dc.window
        && request.selection == atoms[A_Clipboard]
        && request.property != None) {
        if (request.target == atoms[A_Targets]) {
            Atom avaliable_targets[] = {atoms[A_ImagePng]};
            XChangeProperty(
                request.display,
                request.requestor,
                request.property,
                XA_ATOM,
                32,
                PropModeReplace,
                (unsigned char const*)avaliable_targets,
                LENGTH(avaliable_targets)
            );
        } else if (request.target == atoms[A_ImagePng]) {
            trace("requested image/png");
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
            if (png_imdyn == NULL) {
                die("stbi: %s", stbi_failure_reason());
            }

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
    } else {
        trace("xpaint: invalid selection request event received");
    }
    return True;
}

Bool selection_notify_hdlr(struct Ctx* ctx, XEvent* event) {
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
    return True;
}

Bool client_message_hdlr(struct Ctx* ctx, XEvent* event) {
    // close window on request
    return False;
}

void cleanup(struct Ctx* ctx) {
    /* global */ {
        for (u32 i = 0; i < I_Last; ++i) {
            if (images[i] != NULL) {
                XDestroyImage(images[i]);
            }
        }
    }
    /* file paths */ {
        file_ctx_free(&ctx->fout);
        file_ctx_free(&ctx->finp);
    }
    /* SelectionBuffer */ {
        if (ctx->sel_buf.im != NULL) {
            XDestroyImage(ctx->sel_buf.im);
        }
    }
    /* History */ {
        historyarr_clear(ctx->dc.dp, &ctx->hist_nextarr);
        historyarr_clear(ctx->dc.dp, &ctx->hist_prevarr);
    }
    /* ToolCtx */ {
        for (i32 i = 0; i < TCS_NUM; ++i) {
            tool_ctx_free(&ctx->tcarr[i]);
        }
        arrfree(ctx->tcarr);
    }
    /* Input */ {
        if (ctx->input.t == InputT_Console) {
            cl_free(&ctx->input.d.cl);
        }
    }
    /* DrawCtx */ {
        /* Cache */ {
            if (ctx->dc.cache.pm != 0) {
                XFreePixmap(ctx->dc.dp, ctx->dc.cache.pm);
            }
        }
        /* Scheme */ {  // depends on VisualInfo and Colormap
            for (i32 i = 0; i < SchmLast; ++i) {
                for (i32 j = 0; j < 2; ++j) {
                    XftColorFree(
                        ctx->dc.dp,
                        ctx->dc.vinfo.visual,
                        ctx->dc.colmap,
                        j ? &ctx->dc.schemes_dyn[i].bg
                          : &ctx->dc.schemes_dyn[i].fg
                    );
                }
            }
            free(ctx->dc.schemes_dyn);
        }
        fnt_free(ctx->dc.dp, &ctx->dc.fnt);
        canvas_free(ctx->dc.dp, &ctx->dc.cv);
        XdbeDeallocateBackBufferName(ctx->dc.dp, ctx->dc.back_buffer);
        XFreeGC(ctx->dc.dp, ctx->dc.gc);
        XFreeGC(ctx->dc.dp, ctx->dc.screen_gc);
        XFreeColormap(ctx->dc.dp, ctx->dc.colmap);
        XDestroyWindow(ctx->dc.dp, ctx->dc.window);
    }
}

static void
main_arg_bound_check(char const* cmd_name, i32 argc, char** argv, u32 pos) {
    if (pos + 1 == argc || argv[pos + 1][0] == '-') {
        die("xpaint: supply argument for %s", cmd_name);
    }
}
