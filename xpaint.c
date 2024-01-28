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
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

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
#define PI               (3.141)
// default value for signed integers
#define NIL              (-1)
#define UTF_INVALID      (0xFFFD)
#define UTF_SIZ          (4)

#define CURR_TC(p_ctx) ((p_ctx)->tcs[(p_ctx)->curr_tc])
#define CURR_COL(p_tc) ((p_tc)->sdata.colors_argb[(p_tc)->sdata.curr_col])
// clang-format off
#define HAS_SELECTION(p_tc) \
    ((p_tc)->type == Tool_Selection \
    && (p_tc)->data.sel.ex != NIL && (p_tc)->data.sel.ey != NIL \
    && (p_tc)->data.sel.bx != NIL && (p_tc)->data.sel.by != NIL \
    && (p_tc)->data.sel.ex != (p_tc)->data.sel.bx \
    && (p_tc)->data.sel.ey != (p_tc)->data.sel.by)
#define SELECTION_DRAGGING(p_tc) \
    ((p_tc)->type == Tool_Selection \
    && (p_tc)->data.sel.drag_from.x != NIL \
    && (p_tc)->data.sel.drag_from.y != NIL)
// clang-format on

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

struct SelectonCircleDims {
    struct CircleDims {
        u32 x;
        u32 y;
        u32 r;
    } outer, inner;
};

struct Ctx {
    struct DrawCtx {
        Display* dp;
        XVisualInfo vinfo;
        GC gc;
        GC screen_gc;
        Window window;
        u32 width;
        u32 height;
        XdbeBackBuffer back_buffer;  // double buffering
        u32 canvas_zoom;  // 1 == no zoom
        Pair canvas_scroll;
        struct Canvas {
            XImage* im;
            u32 width;
            u32 height;
        } cv;
        struct Fnt {
            XftFont* xfont;
            u32 h;
        } fnt;
    } dc;
    struct ToolCtx {
        void (*on_press)(struct DrawCtx*, struct ToolCtx*, XButtonPressedEvent const*);
        void (*on_release)(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
        void (*on_drag)(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);
        void (*on_move)(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);
        // static zero terminated string pointer
        char* ssz_tool_name;
        i32 prev_x;
        i32 prev_y;
        Bool is_holding;
        Bool is_dragging;
        enum ToolType {
            Tool_Selection,
            Tool_Pencil,
            Tool_Fill,
            Tool_Picker,
        } type;
        struct ToolSharedData {
            u32* colors_argb;
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
                u64 last_proc_drag_ev_us;
                Pair last_processed_pointer;
            } pencil;
        } data;
    }* tcs;
    u32 curr_tc;
    struct History {
        struct Canvas cv;
    } *hist_prev, *hist_next;
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
    struct Input {
        enum InputState {
            InputS_Interact,
            InputS_Color,
        } state;
        union InputData {
            struct InputColorData {
                u32 current_digit;
            } col;
        } data;
    } input;
};

// clang-format off
static void die(char const* errstr, ...);
static void trace(char const* fmt, ...);
static void* ecalloc(u32 n, u32 size);
static u32 digit_count(u32 number);

static Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p);
static Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Pair point_from_cv_to_scr_no_move(struct DrawCtx const* dc, Pair p);
static Pair point_from_scr_to_cv(struct DrawCtx const* dc, Pair p);
static Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y);
static Bool point_in_rect(Pair p, Pair a1, Pair a2);

static XImage* read_png_file(struct DrawCtx const* dc, char const* file_name);

static void init_sel_circ_tools(struct SelectionCircle* sc, i32 x, i32 y);
static void free_sel_circ(struct SelectionCircle* sel_circ);
static i32 current_sel_circ_item(struct SelectionCircle const* sc, i32 x, i32 y);
static struct SelectonCircleDims get_curr_sel_dims(struct SelectionCircle const* sel_circ);

static void set_current_tool_selection(struct ToolCtx* tc);
static void set_current_tool_pencil(struct ToolCtx* tc);
static void set_current_tool_fill(struct ToolCtx* tc);
static void set_current_tool_picker(struct ToolCtx* tc);
static void set_current_input_state(struct Input* input, enum InputState is);

static void tool_selection_on_press(struct DrawCtx* dc, struct ToolCtx* tc, XButtonPressedEvent const* event);
static void tool_selection_on_release(struct DrawCtx* dc, struct ToolCtx* tc, XButtonReleasedEvent const* event);
static void tool_selection_on_drag(struct DrawCtx* dc, struct ToolCtx* tc, XMotionEvent const* event);
static void tool_pencil_on_release(struct DrawCtx* dc, struct ToolCtx* tc, XButtonReleasedEvent const* event);
static void tool_pencil_on_drag(struct DrawCtx* dc, struct ToolCtx* tc, XMotionEvent const* event);
static void tool_pencil_on_move(struct DrawCtx* dc, struct ToolCtx* tc, XMotionEvent const* event);
static void tool_fill_on_release(struct DrawCtx* dc, struct ToolCtx* tc, XButtonReleasedEvent const* event);
static void tool_picker_on_release(struct DrawCtx* dc, struct ToolCtx* tc, XButtonReleasedEvent const* event);

static Bool history_move(struct Ctx* ctx, Bool forward);
static Bool history_push(struct History** hist, struct Ctx* ctx);

static void historyarr_clear(Display* dp, struct History** hist);
static void canvas_clear(Display* dp, struct Canvas* cv);

static void canvas_fill(struct DrawCtx* dc, u32 color);
static void canvas_line(struct DrawCtx* dc, Pair from, Pair to, u32 color, u32 line_w);
static void canvas_point(struct DrawCtx* dc, Pair c, u32 col, u32 line_w);
static void canvas_circle(struct DrawCtx* dc, Pair center, u32 r, u32 col, Bool fill);
static void canvas_copy_region(struct DrawCtx* dc, Pair from, Pair dims, Pair to);
static void resize_canvas(struct DrawCtx* dc, i32 new_width, i32 new_height);

static void draw_selection_circle(struct DrawCtx* dc, struct SelectionCircle const* sc, i32 pointer_x, i32 pointer_y);
static void clear_selection_circle(struct DrawCtx* dc, struct SelectionCircle* sc);
static void update_screen(struct Ctx* ctx);

static struct Ctx setup(Display* dp);
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
// clang-format on

static Bool is_verbose_output = False;
static Atom atoms[A_Last];
static XImage* images[I_Last];

i32 main(i32 argc, char** argv) {
    for (i32 i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
            is_verbose_output = True;
        } else {
            die("usage: xpaint [-v]");
        }
    }

    Display* display = XOpenDisplay(NULL);
    if (!display) {
        die("xpaint: cannot open X display");
    }

    /* extentions support */ {
        i32 maj = NIL;
        i32 min = NIL;
        if (!XdbeQueryExtension(display, &maj, &min)) {
            die("no X Double Buffer Extention support");
        }
    }

    struct Ctx ctx = setup(display);
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

Pair point_from_cv_to_scr(struct DrawCtx const* dc, Pair p) {
    return point_from_cv_to_scr_xy(dc, p.x, p.y);
}

Pair point_from_cv_to_scr_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = x * (i32)dc->canvas_zoom - dc->canvas_scroll.x,
        .y = y * (i32)dc->canvas_zoom - dc->canvas_scroll.y,
    };
}

Pair point_from_cv_to_scr_no_move(struct DrawCtx const* dc, Pair p) {
    return (Pair
    ) {.x = p.x * (i32)dc->canvas_zoom, .y = p.y * (i32)dc->canvas_zoom};
}

Pair point_from_scr_to_cv(struct DrawCtx const* dc, Pair p) {
    return point_from_scr_to_cv_xy(dc, p.x, p.y);
}

Pair point_from_scr_to_cv_xy(struct DrawCtx const* dc, i32 x, i32 y) {
    return (Pair) {
        .x = (x + dc->canvas_scroll.x) / (i32)dc->canvas_zoom,
        .y = (y + dc->canvas_scroll.y) / (i32)dc->canvas_zoom,
    };
}

static Bool point_in_rect(Pair p, Pair a1, Pair a2) {
    return MIN(a1.x, a2.x) < p.x && p.x < MAX(a1.x, a2.x)
        && MIN(a1.y, a2.y) < p.y && p.y < MAX(a1.y, a2.y);
}

XImage* read_png_file(struct DrawCtx const* dc, char const* file_name) {
    i32 width = NIL;
    i32 height = NIL;
    i32 comp = NIL;
    stbi_uc* image_data = stbi_load(file_name, &width, &height, &comp, 4);
    if (image_data == NULL) {
        return NULL;
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

struct SelectonCircleDims
get_curr_sel_dims(struct SelectionCircle const* sel_circ) {
    return (struct SelectonCircleDims) {
        .outer =
            {
                .x = sel_circ->x - SELECTION_CIRCLE.outer_r_px,
                .y = sel_circ->y - SELECTION_CIRCLE.outer_r_px,
                .r = SELECTION_CIRCLE.outer_r_px,
            },
        .inner =
            {
                .x = sel_circ->x - SELECTION_CIRCLE.inner_r_px,
                .y = sel_circ->y - SELECTION_CIRCLE.inner_r_px,
                .r = SELECTION_CIRCLE.inner_r_px,
            },
    };
}

static void set_current_tool(struct ToolCtx* tc, enum ToolType type) {
    struct ToolCtx new_tc = {
        .type = type,
        .prev_x = tc->prev_x,
        .prev_y = tc->prev_y,
        .sdata = tc->sdata,
    };
    switch (type) {
        case Tool_Selection:
            new_tc.on_press = &tool_selection_on_press;
            new_tc.on_release = &tool_selection_on_release;
            new_tc.on_drag = &tool_selection_on_drag;
            new_tc.ssz_tool_name = "selection";
            new_tc.data.sel = (struct SelectionData) {
                .by = NIL,
                .bx = NIL,
                .ey = NIL,
                .ex = NIL,
                .drag_from = {NIL, NIL},
                .drag_to = {NIL, NIL},
            };
            break;
        case Tool_Pencil:
            new_tc.on_release = &tool_pencil_on_release;
            new_tc.on_drag = &tool_pencil_on_drag;
            new_tc.on_move = &tool_pencil_on_move;
            new_tc.ssz_tool_name = "pencil";
            new_tc.data.pencil = (struct PencilData) {
                .line_r = 5,
                .last_proc_drag_ev_us = 0,
                .last_processed_pointer = (Pair) {NIL, NIL},
            };
            break;
        case Tool_Fill:
            new_tc.on_release = &tool_fill_on_release;
            new_tc.ssz_tool_name = "fill";
            break;
        case Tool_Picker:
            new_tc.on_release = &tool_picker_on_release;
            new_tc.ssz_tool_name = "color picker";
            break;
    }
    *tc = new_tc;
}

void set_current_tool_selection(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Selection);
}

void set_current_tool_pencil(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Pencil);
}

void set_current_tool_fill(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Fill);
}

void set_current_tool_picker(struct ToolCtx* tc) {
    set_current_tool(tc, Tool_Picker);
}

void set_current_input_state(struct Input* input, enum InputState const is) {
    input->state = is;

    switch (is) {
        case InputS_Color:
            input->data.col = (struct InputColorData) {.current_digit = 0};
            break;
        case InputS_Interact:
            break;
    }
}

void tool_selection_on_press(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XButtonPressedEvent const* event
) {
    assert(tc->type == Tool_Selection);
    if (event->button == XLeftMouseBtn) {
        struct SelectionData* sd = &tc->data.sel;
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
    XButtonReleasedEvent const* event
) {
    assert(tc->type == Tool_Selection);
    if (event->button == XLeftMouseBtn) {
        struct SelectionData* sd = &tc->data.sel;

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
                (Pair
                ) {MAX(sd->bx, sd->ex) - area.x, MAX(sd->by, sd->ey) - area.y},
                (Pair) {area.x + move_vec.x, area.y + move_vec.y}
            );
        } else if (tc->is_dragging) {
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
}

void tool_selection_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XMotionEvent const* event
) {
    assert(tc->type == Tool_Selection);
    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    if (SELECTION_DRAGGING(tc)) {
        tc->data.sel.drag_to = pointer;
    } else if (tc->is_holding) {
        tc->data.sel.ex = CLAMP(pointer.x, 0, dc->cv.width);
        tc->data.sel.ey = CLAMP(pointer.y, 0, dc->cv.height);
    }
}

void tool_pencil_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XButtonReleasedEvent const* event
) {
    assert(tc->type == Tool_Pencil);

    if (event->button == XLeftMouseBtn && !tc->is_dragging) {
        Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
        canvas_point(dc, pointer, CURR_COL(tc), tc->sdata.line_w);
    }
}

void tool_pencil_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XMotionEvent const* event
) {
    assert(tc->type == Tool_Pencil);
    assert(tc->is_holding);

    struct PencilData* pd = &tc->data.pencil;

    struct timeval current_time;
    gettimeofday(&current_time, 0x0);
    if (current_time.tv_usec - pd->last_proc_drag_ev_us
        < PENCIL_TOOL.drag_period_us) {
        return;
    }
    pd->last_proc_drag_ev_us = current_time.tv_usec;

    Pair pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);
    Pair prev_pointer = (pd->last_processed_pointer.x != NIL)
        ? point_from_scr_to_cv(dc, pd->last_processed_pointer)
        : pointer;
    canvas_line(dc, prev_pointer, pointer, CURR_COL(tc), tc->sdata.line_w);
    pd->last_processed_pointer = (Pair) {event->x, event->y};
}

void tool_pencil_on_move(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XMotionEvent const* event
) {
    assert(tc);
    assert(tc->type == Tool_Pencil);

    tc->data.pencil.last_proc_drag_ev_us = 0;
    tc->data.pencil.last_processed_pointer = (Pair) {NIL, NIL};
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

    Pair* queue = NULL;
    Pair first = {x, y};
    arrpush(queue, first);

    while (arrlen(queue)) {
        Pair curr = arrpop(queue);

        for (i32 dir = 0; dir < 4; ++dir) {
            Pair d_curr = {curr.x + d_rows[dir], curr.y + d_cols[dir]};

            if (d_curr.x < 0 || d_curr.y < 0 || d_curr.x >= im->width
                || d_curr.y >= im->height) {
                continue;
            }

            if (XGetPixel(im, d_curr.x, d_curr.y) == area_col) {
                XPutPixel(im, d_curr.x, d_curr.y, targ_col);
                arrpush(queue, d_curr);
            }
        }
    }

    arrfree(queue);
}

void tool_fill_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XButtonReleasedEvent const* event
) {
    Pair const pointer = point_from_scr_to_cv_xy(dc, event->x, event->y);

    flood_fill(dc->cv.im, CURR_COL(tc), pointer.x, pointer.y);
}

void tool_picker_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
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
    struct History** hist_pop = forward ? &ctx->hist_prev : &ctx->hist_next;
    struct History** hist_save = forward ? &ctx->hist_next : &ctx->hist_prev;

    if (!arrlenu(*hist_pop)) {
        return False;
    }

    struct History const curr = arrpop(*hist_pop);
    history_push(hist_save, ctx);

    canvas_clear(ctx->dc.dp, &ctx->dc.cv);

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

void historyarr_clear(Display* dp, struct History** hist) {
    for (u32 i = 0; i < arrlenu(*hist); ++i) {
        struct History* h = &(*hist)[i];
        canvas_clear(dp, &h->cv);
    }
    arrfree(*hist);
}

void canvas_clear(Display* dp, struct Canvas* cv) {
    XDestroyImage(cv->im);
}

void init_sel_circ_tools(struct SelectionCircle* sc, i32 x, i32 y) {
    static struct Item tools[] = {
        {.on_select = &set_current_tool_selection, .hicon = I_Select},
        {.on_select = &set_current_tool_pencil, .hicon = I_Pencil},
        {.on_select = &set_current_tool_fill, .hicon = I_Fill},
        {.on_select = &set_current_tool_picker, .hicon = I_Picker},
    };

    sc->is_active = True;
    sc->x = x;
    sc->y = y;
    sc->item_count = LENGTH(tools);
    sc->items = tools;
}

i32 current_sel_circ_item(struct SelectionCircle const* sc, i32 x, i32 y) {
    struct SelectonCircleDims sel_rect = get_curr_sel_dims(sc);
    i32 const pointer_x_rel = x - sc->x;
    i32 const pointer_y_rel = y - sc->y;
    double const segment_rad = PI * 2 / MAX(1, sc->item_count);
    double const segment_deg = segment_rad / PI * 180;
    double const pointer_r =
        sqrt(pointer_x_rel * pointer_x_rel + pointer_y_rel * pointer_y_rel);

    if (pointer_r > sel_rect.outer.r || pointer_r < sel_rect.inner.r) {
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
    canvas_circle(dc, c, line_w / 2, col, True);
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

void canvas_copy_region(struct DrawCtx* dc, Pair from, Pair dims, Pair to) {
    if (from.x < 0 || from.y < 0 || to.x < 0 || to.y < 0) {
        return;
    }
    XImage* im = dc->cv.im;
    i32 w = im->width;
    i32 h = im->height;
    for (i32 y = 0; y < dims.y; ++y) {
        if (from.y + y >= h || to.y + y >= h) {
            break;
        }
        for (i32 x = 0; x < dims.x; ++x) {
            if (from.x + x >= w || to.x + x >= w) {
                break;
            }
            u32 pixel = XGetPixel(im, from.x + x, from.y + y);
            XPutPixel(im, to.x + x, to.y + y, pixel);
        }
    }
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

    struct SelectonCircleDims sel_rect = get_curr_sel_dims(sc);

    XSetLineAttributes(
        dc->dp,
        dc->screen_gc,
        SELECTION_CIRCLE.line_w,
        SELECTION_CIRCLE.line_style,
        SELECTION_CIRCLE.cap_style,
        SELECTION_CIRCLE.join_style
    );
    XSetForeground(dc->dp, dc->screen_gc, SELECTION_CIRCLE.background_argb);
    XFillArc(
        dc->dp,
        dc->window,
        dc->screen_gc,
        (i32)sel_rect.outer.x,
        (i32)sel_rect.outer.y,
        sel_rect.outer.r * 2,
        sel_rect.outer.r * 2,
        0,
        360 * 64
    );

    XSetForeground(dc->dp, dc->screen_gc, SELECTION_CIRCLE.line_col_argb);
    XDrawArc(
        dc->dp,
        dc->window,
        dc->screen_gc,
        (i32)sel_rect.inner.x,
        (i32)sel_rect.inner.y,
        sel_rect.inner.r * 2,
        sel_rect.inner.r * 2,
        0,
        360 * 64
    );

    XDrawArc(
        dc->dp,
        dc->window,
        dc->screen_gc,
        (i32)sel_rect.outer.x,
        (i32)sel_rect.outer.y,
        sel_rect.outer.r * 2,
        sel_rect.outer.r * 2,
        0,
        360 * 64
    );

    {
        double const segment_rad = PI * 2 / MAX(1, sc->item_count);
        double const segment_deg = segment_rad / PI * 180;

        if (sc->item_count >= 2) {
            for (u32 line_num = 0; line_num < sc->item_count; ++line_num) {
                XDrawLine(
                    dc->dp,
                    dc->window,
                    dc->screen_gc,
                    sc->x
                        + (i32)(cos(segment_rad * line_num) * sel_rect.inner.r),
                    sc->y
                        + (i32)(sin(segment_rad * line_num) * sel_rect.inner.r),
                    sc->x
                        + (i32)(cos(segment_rad * line_num) * sel_rect.outer.r),
                    sc->y
                        + (i32)(sin(segment_rad * line_num) * sel_rect.outer.r)
                );
            }
        }

        for (u32 item = 0; item < sc->item_count; ++item) {
            XImage* image = images[sc->items[item].hicon];
            assert(image != NULL);

            // FIXME use XRender transparency
            XPutImage(
                dc->dp,
                dc->window,
                dc->screen_gc,
                image,
                0,
                0,
                (i32)(sc->x
                      + cos(-segment_rad * (item + 0.5))
                          * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5)
                      - image->width / 2.0),
                (i32)(sc->y
                      + sin(-segment_rad * (item + 0.5))
                          * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5)
                      - image->height / 2.0),
                image->width,
                image->height
            );
        }

        // pointer
        i32 const current_item =
            current_sel_circ_item(sc, pointer_x, pointer_y);
        if (current_item != NIL) {
            XSetForeground(
                dc->dp,
                dc->screen_gc,
                SELECTION_CIRCLE.active_background_argb
            );
            XFillArc(
                dc->dp,
                dc->window,
                dc->screen_gc,
                (i32)sel_rect.outer.x,
                (i32)sel_rect.outer.y,
                sel_rect.outer.r * 2,
                sel_rect.outer.r * 2,
                (i32)(current_item * segment_deg) * 64,
                (i32)segment_deg * 64
            );
            XSetForeground(
                dc->dp,
                dc->screen_gc,
                SELECTION_CIRCLE.active_inner_background_argb
            );
            XFillArc(
                dc->dp,
                dc->window,
                dc->screen_gc,
                (i32)sel_rect.inner.x,
                (i32)sel_rect.inner.y,
                sel_rect.inner.r * 2,
                sel_rect.inner.r * 2,
                (i32)(current_item * segment_deg) * 64,
                (i32)segment_deg * 64
            );
        }
    }
}

void clear_selection_circle(struct DrawCtx* dc, struct SelectionCircle* sc) {
    struct SelectonCircleDims sel_rect = get_curr_sel_dims(sc);

    XClearArea(
        dc->dp,
        dc->window,
        (i32)sel_rect.outer.x - 1,
        (i32)sel_rect.outer.y - 1,
        sel_rect.outer.r * 2 + 2,
        sel_rect.outer.r * 2 + 2,
        True  // Expose to draw background
    );
}

static i64 utf8decodebyte(const char c, usize* i) {
    static unsigned char const UTFBYTE[UTF_SIZ + 1] =
        {0x80, 0, 0xC0, 0xE0, 0xF0};
    static unsigned char const UTFMASK[UTF_SIZ + 1] =
        {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

    for (*i = 0; *i < (UTF_SIZ + 1); ++(*i)) {
        if (((unsigned char)c & UTFMASK[*i]) == UTFBYTE[*i]) {
            return (unsigned char)c & ~UTFMASK[*i];
        }
    }
    return 0;
}

static usize utf8validate(i64* u, usize i) {
    static i64 const UTFMIN[UTF_SIZ + 1] = {0, 0, 0x80, 0x800, 0x10000};
    static i64 const UTFMAX[UTF_SIZ + 1] =
        {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

    if (!BETWEEN(*u, UTFMIN[i], UTFMAX[i]) || BETWEEN(*u, 0xD800, 0xDFFF)) {
        *u = UTF_INVALID;
    }
    for (i = 1; *u > UTFMAX[i]; ++i) {
        ;
    }
    return i;
}

static u64 utf8decode(const char* c, i64* u, usize clen) {
    *u = UTF_INVALID;
    if (!clen) {
        return 0;
    }
    usize len;

    i64 udecoded = utf8decodebyte(c[0], &len);
    if (!BETWEEN(len, 1, UTF_SIZ)) {
        return 1;
    }
    usize j = 1;
    for (usize i = 1; i < clen && j < len; ++i, ++j) {
        usize type;
        udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
        if (type) {
            return j;
        }
    }
    if (j < len) {
        return 0;
    }
    *u = udecoded;
    utf8validate(u, len);

    return len;
}

// FIXME use themes to pass color (dc)
static void draw_string(struct DrawCtx* dc, char const* str, Pair c, u32 col) {
    Colormap cm = DefaultColormap(dc->dp, DefaultScreen(dc->dp));
    XftDraw* d = XftDrawCreate(dc->dp, dc->back_buffer, dc->vinfo.visual, cm);
    u32 utf8strlen = 0;
    for (char const* s = str; *s; ++s) {
        i64 utf8codepoint = 0;
        utf8strlen += utf8decode(s, &utf8codepoint, UTF_SIZ);
    }
    XftColor xft_col;
    // FIXME use themes to pass color
    char xft_col_name[6 + 1 + 1];
    sprintf(xft_col_name, "#%X", col & 0xFFFFFF);
    XftColorAllocName(dc->dp, dc->vinfo.visual, cm, xft_col_name, &xft_col);
    XftDrawStringUtf8(
        d,
        &xft_col,
        dc->fnt.xfont,
        c.x,
        c.y,
        (XftChar8*)str,
        (i32)utf8strlen
    );
    XftDrawDestroy(d);
}

static void draw_int(struct DrawCtx* dc, i32 i, Pair c, u32 col) {
    static u32 const MAX_SIZE = 50;
    assert(digit_count(i) < MAX_SIZE);

    char buf[MAX_SIZE];
    memset(buf, '\0', MAX_SIZE);  // FIXME memset_s
    sprintf(buf, "%d", i);
    draw_string(dc, buf, c, col);
}

static int fill_rect(struct DrawCtx* dc, Pair p, Pair dim, u32 col) {
    XSetForeground(dc->dp, dc->screen_gc, col);
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
    char buf[50] = {0};  // XXX magic value
    sprintf(buf, format, i);
    return get_string_width(dc, buf, strlen(buf));
}

void update_screen(struct Ctx* ctx) {
    /* draw canvas */ {
        fill_rect(
            &ctx->dc,
            (Pair) {0, 0},
            (Pair) {(i32)ctx->dc.width, (i32)ctx->dc.height},
            WINDOW.background_rgb
        );
        /* put scaled image */ {
            //  https://stackoverflow.com/a/66896097
            u32 pb_width = ctx->dc.cv.width * ctx->dc.canvas_zoom;
            u32 pb_height = ctx->dc.cv.height * ctx->dc.canvas_zoom;
            Pixmap pixel_buffer = XCreatePixmap(
                ctx->dc.dp,
                ctx->dc.window,
                pb_width,
                pb_height,
                ctx->dc.vinfo.depth
            );
            XPutImage(
                ctx->dc.dp,
                pixel_buffer,
                ctx->dc.screen_gc,
                ctx->dc.cv.im,
                0,
                0,
                0,
                0,
                pb_width,
                pb_height
            );

            XRenderPictFormat* pict_format =
                XRenderFindStandardFormat(ctx->dc.dp, PictStandardARGB32);
            assert(pict_format);
            XRenderPictureAttributes pict_attrs = {
                .subwindow_mode = IncludeInferiors
            };

            Picture src_pict = XRenderCreatePicture(
                ctx->dc.dp,
                pixel_buffer,
                pict_format,
                0,
                &pict_attrs
            );
            Picture dst_pict = XRenderCreatePicture(
                ctx->dc.dp,
                ctx->dc.back_buffer,
                pict_format,
                0,
                &pict_attrs
            );

            // clang-format off
            double const z = 1.0 / ctx->dc.canvas_zoom;
            XRenderSetPictureTransform(ctx->dc.dp, src_pict, &(XTransform){{
                { XDoubleToFixed(z), XDoubleToFixed(0), XDoubleToFixed(0) },
                { XDoubleToFixed(0), XDoubleToFixed(z), XDoubleToFixed(0) },
                { XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1) },
            }});

            XRenderComposite(
                ctx->dc.dp, PictOpSrc,
                src_pict, 0,
                dst_pict,
                0, 0,
                0, 0,
                -ctx->dc.canvas_scroll.x, -ctx->dc.canvas_scroll.y,
                pb_width, pb_height
            );
            // clang-format on
            XRenderFreePicture(ctx->dc.dp, src_pict);
            XRenderFreePicture(ctx->dc.dp, dst_pict);
            XFreePixmap(ctx->dc.dp, pixel_buffer);
        }
    }
    /* current selection */ {
        if (HAS_SELECTION(&CURR_TC(ctx))) {
            struct SelectionData sd = CURR_TC(ctx).data.sel;
            Pair p = {MIN(sd.bx, sd.ex), MIN(sd.by, sd.ey)};
            Pair dim = {MAX(sd.bx, sd.ex) - p.x, MAX(sd.by, sd.ey) - p.y};
            draw_rect(
                &ctx->dc,
                point_from_cv_to_scr(&ctx->dc, p),
                point_from_cv_to_scr_no_move(&ctx->dc, dim),
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
                    &ctx->dc,
                    point_from_cv_to_scr_xy(&ctx->dc, p.x + dx, p.y + dy),
                    point_from_cv_to_scr_no_move(&ctx->dc, dim),
                    SELECTION_TOOL.drag_argb,
                    SELECTION_TOOL.line_w,
                    SELECTION_TOOL.line_style,
                    SELECTION_TOOL.cap_style,
                    SELECTION_TOOL.join_style
                );
            }
        }
    }
    /* statusline */ {
        struct DrawCtx* dc = &ctx->dc;
        struct ToolCtx* tc = &CURR_TC(ctx);
        u32 const statusline_h = dc->fnt.xfont->ascent;
        XSetForeground(dc->dp, dc->screen_gc, STATUSLINE.background_argb);
        XFillRectangle(
            dc->dp,
            dc->back_buffer,
            dc->screen_gc,
            0,
            (i32)(dc->height - statusline_h),
            dc->width,
            statusline_h
        );
        /* captions */ {
            // clang-format off
            static Bool statics_initialized = False;
            static u32 const gap = 5;
            static u32 const small_gap = gap / 2;
            static u32 input_state_w = 0;
            static u32 col_name_w = 0;
            static u32 tcs_w = 0;
            static u32 col_count_w = 0;
            static u32 tool_name_w = 0;
            if (!statics_initialized) {
                // XXX uses 'F' as average character
                /* col_count_w */ {
                    col_count_w =
                        get_string_width(dc, "/", 1) +
                        get_int_width(dc, "%d", MAX_COLORS) * 2 +
                        gap;
                }
                /* tcs_w */ {
                    for (i32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                        tcs_w += get_int_width(dc, "%d", tc_name) + small_gap;
                    }
                    tcs_w += gap;
                }
                col_name_w = get_string_width(dc, "#FFFFFF", 7) + gap;
                // FIXME how many symbols?
                input_state_w = get_string_width(dc, "FFFFFFFFFFFFFF", 14) + gap;
                tool_name_w = get_string_width(dc, "FFFFFFFFFFFFFF", 14) + gap;
                statics_initialized = True;
            }
            Pair const tcs_c = {0, (i32)dc->height};
            Pair const input_state_c = { (i32)(tcs_c.x + tcs_w), (i32)dc->height };
            Pair const tool_name_c = { (i32)(input_state_c.x + input_state_w), (i32)dc->height };
            Pair const line_w_c = { (i32)(tool_name_c.x + tool_name_w), (i32)dc->height };
            Pair const col_count_c = { (i32)(dc->width - col_count_w), (i32)dc->height };
            Pair const col_c = { (i32)(col_count_c.x - col_name_w), (i32)dc->height };
            // clang-format on
            // colored rectangle
            static u32 const col_rect_w = 30;
            static u32 const col_value_size = 1 + 6;

            XSetBackground(dc->dp, dc->screen_gc, STATUSLINE.background_argb);
            XSetForeground(dc->dp, dc->screen_gc, STATUSLINE.font_argb);
            /* tc */ {
                i32 x = tcs_c.x;
                for (i32 tc_name = 1; tc_name <= TCS_NUM; ++tc_name) {
                    draw_int(
                        dc,
                        tc_name,
                        (Pair) {x, tcs_c.y},
                        ctx->curr_tc == (tc_name - 1)
                            ? STATUSLINE.strong_font_argb
                            : STATUSLINE.font_argb
                    );
                    x += (i32)(get_int_width(dc, "%d", tc_name) + small_gap);
                }
            }
            /* input state */ {
                draw_string(
                    dc,
                    ctx->input.state == InputS_Interact    ? "intearct"
                        : ctx->input.state == InputS_Color ? "color"
                                                           : "unknown",
                    input_state_c,
                    STATUSLINE.font_argb
                );
            }
            draw_string(
                dc,
                tc->ssz_tool_name ? tc->ssz_tool_name : "N/A",
                tool_name_c,
                STATUSLINE.font_argb
            );
            draw_int(dc, (i32)tc->sdata.line_w, line_w_c, STATUSLINE.font_argb);
            /* color */ {
                char col_value[col_value_size + 2];  // FIXME ?
                sprintf(col_value, "#%06X", CURR_COL(tc) & 0xFFFFFF);
                draw_string(dc, col_value, col_c, STATUSLINE.font_argb);
                /* color count */ {
                    // FIXME how it possible
                    char col_count[digit_count(MAX_COLORS) * 2 + 1 + 1];
                    sprintf(
                        col_count,
                        "%d/%td",
                        tc->sdata.curr_col + 1,
                        arrlen(tc->sdata.colors_argb)
                    );
                    draw_string(
                        dc,
                        col_count,
                        col_count_c,
                        STATUSLINE.font_argb
                    );
                }
                if (ctx->input.state == InputS_Color) {
                    static u32 const hash_w = 1;
                    u32 const curr_dig = ctx->input.data.col.current_digit;
                    char const col_digit_value[] =
                        {[0] = col_value[curr_dig + hash_w], [1] = '\0'};
                    draw_string(
                        dc,
                        col_digit_value,
                        (Pair
                        ) {col_c.x
                               + (i32)get_string_width(
                                   dc,
                                   col_value,
                                   curr_dig + hash_w
                               ),
                           (i32)dc->height},
                        STATUSLINE.strong_font_argb
                    );
                }
                XSetForeground(dc->dp, dc->screen_gc, CURR_COL(tc));
                XFillRectangle(
                    dc->dp,
                    dc->back_buffer,
                    dc->screen_gc,
                    (i32)(dc->width - col_name_w - col_rect_w - col_count_w),
                    (i32)(dc->height - statusline_h),
                    col_rect_w,
                    statusline_h
                );
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
}

void resize_canvas(struct DrawCtx* dc, i32 new_width, i32 new_height) {
    if (new_width <= 0 || new_height <= 0) {
        trace("resize_canvas: invalid canvas size");
        return;
    }

    XImage* new_cv_im = XSubImage(dc->cv.im, 0, 0, new_width, new_height);

    XDestroyImage(dc->cv.im);

    dc->cv.im = new_cv_im;
    dc->cv.width = new_width;
    dc->cv.height = new_height;
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
        if (handlers[event.type]) {
            running = handlers[event.type](ctx, &event);
        }
    }
}

struct Ctx setup(Display* dp) {
    assert(dp);
    struct Ctx ctx = {
        .dc.dp = dp,
        .dc.width = CANVAS.default_width,
        .dc.height = CANVAS.default_height,
        .dc.canvas_zoom = 1,
        .dc.canvas_scroll = {0, 0},
        .hist_next = NULL,
        .hist_prev = NULL,
        .sel_buf.im = NULL,
        .input.state = InputS_Interact,
        .curr_tc = 0,
    };

    /* init arrays */ {
        for (i32 i = 0; i < TCS_NUM; ++i) {
            struct ToolCtx tc = {
                .sdata.colors_argb = NULL,
                .sdata.curr_col = 0,
                .sdata.line_w = 5,
                .prev_x = NIL,
                .prev_y = NIL,
            };
            arrpush(ctx.tcs, tc);
            arrpush(ctx.tcs[i].sdata.colors_argb, 0xFF000000);
            arrpush(ctx.tcs[i].sdata.colors_argb, 0xFFFFFFFF);
        }
    }

    /* atoms */ {
        atoms[A_Clipboard] = XInternAtom(dp, "CLIPBOARD", False);
        atoms[A_Targets] = XInternAtom(dp, "TARGETS", False);
        atoms[A_Utf8string] = XInternAtom(dp, "UTF8_STRING", False);
        atoms[A_ImagePng] = XInternAtom(dp, "image/png", False);
    }

    i32 screen = DefaultScreen(dp);
    Window root = DefaultRootWindow(dp);

    i32 result = XMatchVisualInfo(dp, screen, 32, TrueColor, &ctx.dc.vinfo);
    assert(result != 0);

    /* create window */
    ctx.dc.window = XCreateWindow(
        dp,
        root,
        0,
        0,
        ctx.dc.width,
        ctx.dc.height,
        0,  // border width
        ctx.dc.vinfo.depth,
        InputOutput,
        ctx.dc.vinfo.visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWEventMask,
        &(XSetWindowAttributes
        ) {.colormap =
               XCreateColormap(dp, root, ctx.dc.vinfo.visual, AllocNone),
           .border_pixel = 0,
           .background_pixel = 0xFFFF00FF,
           .event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask
               | ExposureMask | PointerMotionMask | StructureNotifyMask}
    );
    ctx.dc.screen_gc = XCreateGC(dp, ctx.dc.window, 0, 0);

    XSetWMName(
        dp,
        ctx.dc.window,
        &(XTextProperty
        ) {.value = (char unsigned*)title,
           .nitems = strlen(title),
           .format = 8,
           .encoding = atoms[A_Utf8string]}
    );

    ctx.dc.back_buffer = XdbeAllocateBackBufferName(dp, ctx.dc.window, 0);

    /* turn on protocol support */ {
        Atom wm_delete_window = XInternAtom(dp, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dp, ctx.dc.window, &wm_delete_window, 1);
    }

    /* font */ {
        // XXX valgrind detects leak here
        XftFont* xfont = XftFontOpenName(dp, screen, FONT_NAME);
        ctx.dc.fnt.xfont = xfont;
        ctx.dc.fnt.h = xfont->ascent + xfont->descent;
    }

    /* static images */ {
        for (i32 i = 0; i < I_Last; ++i) {
            images[i] = read_png_file(
                &ctx.dc,
                i == I_Select       ? "./res/tool-select.png"
                    : i == I_Pencil ? "./res/tool-pencil.png"
                    : i == I_Fill   ? "./res/tool-fill.png"
                    : i == I_Picker ? "./res/tool-picker.png"
                                    : "null"
            );
        }
    }

    /* canvas */ {
        ctx.dc.cv.width = CANVAS.default_width;
        ctx.dc.cv.height = CANVAS.default_height;
        Pixmap data = XCreatePixmap(
            dp,
            ctx.dc.window,
            ctx.dc.cv.width,
            ctx.dc.cv.height,
            ctx.dc.vinfo.depth
        );
        ctx.dc.cv.im = XGetImage(
            ctx.dc.dp,
            data,
            0,
            0,
            ctx.dc.cv.width,
            ctx.dc.cv.height,
            AllPlanes,
            ZPixmap
        );
        XFreePixmap(ctx.dc.dp, data);
        XGCValues canvas_gc_vals = {
            .line_style = LineSolid,
            .line_width = 5,
            .cap_style = CapButt,
            .fill_style = FillSolid
        };
        ctx.dc.gc = XCreateGC(
            dp,
            ctx.dc.window,
            GCForeground | GCBackground | GCFillStyle | GCLineStyle
                | GCLineWidth | GCCapStyle | GCJoinStyle,
            &canvas_gc_vals
        );
        // initial canvas color
        canvas_fill(&ctx.dc, CANVAS.background_argb);
    }

    for (i32 i = 0; i < TCS_NUM; ++i) {
        set_current_tool_pencil(&ctx.tcs[i]);
    }
    history_push(&ctx.hist_prev, &ctx);

    /* show up window */
    XMapRaised(dp, ctx.dc.window);

    return ctx;
}

Bool button_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    if (e->button == XRightMouseBtn) {
        init_sel_circ_tools(&ctx->sc, e->x, e->y);
        draw_selection_circle(&ctx->dc, &ctx->sc, NIL, NIL);
    }
    if (CURR_TC(ctx).on_press) {
        CURR_TC(ctx).on_press(&ctx->dc, &CURR_TC(ctx), e);
        update_screen(ctx);
    }
    if (e->button == XLeftMouseBtn) {
        // next history invalidated after user action
        historyarr_clear(ctx->dc.dp, &ctx->hist_next);
        history_push(&ctx->hist_prev, ctx);
        CURR_TC(ctx).is_holding = True;
    }

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
        clear_selection_circle(&ctx->dc, &ctx->sc);
        return True;  // something selected do nothing else
    }

    if (e->button == XMouseScrollDown || e->button == XMouseScrollUp) {
        if (e->state & ShiftMask) {
            ctx->dc.canvas_scroll.x += e->button == XMouseScrollDown ? -10 : 10;
        } else {
            ctx->dc.canvas_scroll.y += e->button == XMouseScrollDown ? -10 : 10;
        }
        update_screen(ctx);
    }

    if (CURR_TC(ctx).on_release) {
        CURR_TC(ctx).on_release(&ctx->dc, &CURR_TC(ctx), e);
        update_screen(ctx);
    }
    CURR_TC(ctx).is_holding = False;
    CURR_TC(ctx).is_dragging = False;

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
    assert(input->state == InputS_Color);

    if (input->data.col.current_digit == 0 && !is_increment) {
        input->data.col.current_digit = 5;
    } else if (input->data.col.current_digit == 5 && is_increment) {
        input->data.col.current_digit = 0;
    } else {
        input->data.col.current_digit += is_increment ? 1 : -1;
    }
}

Bool key_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XKeyPressedEvent e = event->xkey;
    if (e.type == KeyRelease) {
        return True;
    }

    KeySym const key_sym = XLookupKeysym(&e, 0);

    HANDLE_KEY_BEGIN(e)
    switch (ctx->input.state) {
        case InputS_Interact: {
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
                    i32 x =
                        MIN(CURR_TC(ctx).data.sel.bx, CURR_TC(ctx).data.sel.ex);
                    i32 y =
                        MIN(CURR_TC(ctx).data.sel.by, CURR_TC(ctx).data.sel.ey);
                    u32 width =
                        MAX(CURR_TC(ctx).data.sel.ex, CURR_TC(ctx).data.sel.bx)
                        - x;
                    u32 height =
                        MAX(CURR_TC(ctx).data.sel.ey, CURR_TC(ctx).data.sel.by)
                        - y;
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
                        for (u32 i = 0; i < MIN(image_size, 32); ++i) {
                            trace("%X", ctx->sel_buf.im->data[i]);
                        }
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
                set_current_input_state(&ctx->input, InputS_Color);
                update_screen(ctx);
            }
            if (key_sym >= XK_1 && key_sym <= XK_9) {
                u32 val = key_sym - XK_1;
                if (val < TCS_NUM) {
                    ctx->curr_tc = val;
                    update_screen(ctx);
                }
            }
            if (key_sym >= XK_Left && key_sym <= XK_Down
                && e.state & ControlMask) {
                u32 const value = e.state & ShiftMask ? 25 : 5;
                resize_canvas(
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
                ctx->dc.canvas_zoom = CLAMP(ctx->dc.canvas_zoom + 1, 1, 4);
                update_screen(ctx);
            }
            HANDLE_KEY_CASE_MASK(ControlMask, XK_minus) {
                ctx->dc.canvas_zoom = CLAMP(ctx->dc.canvas_zoom - 1, 1, 4);
                update_screen(ctx);
            }
        } break;

        case InputS_Color: {
            HANDLE_KEY_CASE_MASK(ControlMask, XK_Up) {
                u32 const len = arrlen(CURR_TC(ctx).sdata.colors_argb);
                if (len != MAX_COLORS) {
                    CURR_TC(ctx).sdata.curr_col = len;
                    arrpush(CURR_TC(ctx).sdata.colors_argb, 0xFF000000);
                    update_screen(ctx);
                }
            }
            HANDLE_KEY_CASE(XK_Right) {
                to_next_input_digit(&ctx->input, True);
                update_screen(ctx);
            }
            HANDLE_KEY_CASE(XK_Left) {
                to_next_input_digit(&ctx->input, False);
                update_screen(ctx);
            }
            if ((key_sym >= XK_0 && key_sym <= XK_9)
                || (key_sym >= XK_a && key_sym <= XK_f)) {
                // current_digit from left to right except alpha (aarrggbb <=> --012345)
                u32 val = key_sym - (key_sym <= XK_9 ? XK_0 : XK_a - 10);
                u32 shift = (5 - ctx->input.data.col.current_digit) * 4;
                CURR_COL(&CURR_TC(ctx)) &= ~(0xF << shift);  // clear
                CURR_COL(&CURR_TC(ctx)) |= val << shift;  // set
                to_next_input_digit(&ctx->input, True);
                update_screen(ctx);
            }
            HANDLE_KEY_CASE(XK_Escape) {  // FIXME XK_c
                set_current_input_state(&ctx->input, InputS_Interact);
                update_screen(ctx);
            }
        } break;

        default:
            assert(False && "unknown input state");
    }
    // independent
    HANDLE_KEY_CASE(XK_q) {
        return False;
    }
    if ((key_sym == XK_Up || key_sym == XK_Down) && !(e.state & ControlMask)) {
        u32 col_num = arrlen(CURR_TC(ctx).sdata.colors_argb);
        assert(col_num != 0);
        CURR_TC(ctx).sdata.curr_col =
            (CURR_TC(ctx).sdata.curr_col + (key_sym == XK_Up ? 1 : -1))
            % col_num;
        update_screen(ctx);
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

    if (CURR_TC(ctx).is_holding) {
        CURR_TC(ctx).is_dragging = True;
        if (CURR_TC(ctx).on_drag) {
            CURR_TC(ctx).on_drag(&ctx->dc, &CURR_TC(ctx), e);
            update_screen(ctx);
        }
    } else {
        if (CURR_TC(ctx).on_move) {
            CURR_TC(ctx).on_move(&ctx->dc, &CURR_TC(ctx), e);
            update_screen(ctx);
        }
    }

    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    CURR_TC(ctx).prev_x = e->x;
    CURR_TC(ctx).prev_y = e->y;

    return True;
}

Bool configure_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    ctx->dc.width = event->xconfigure.width;
    ctx->dc.height = event->xconfigure.height;

    // backbuffer resizes automatically

    return True;
}

// FIXME remove
static unsigned char* ximage_to_rgb(XImage* image, i32 const w, i32 const h) {
    size_t data_size = (size_t)w * h * 3;
    unsigned char* data = (unsigned char*)ecalloc(1, data_size);
    if (data == NULL) {
        return NULL;
    }
    u64 red_mask = image->red_mask;
    u64 green_mask = image->green_mask;
    u64 blue_mask = image->blue_mask;
    i32 ii = 0;
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            u64 pixel = XGetPixel(image, x, y);
            unsigned char blue = (pixel & blue_mask);
            unsigned char green = (pixel & green_mask) >> 8U;
            unsigned char red = (pixel & red_mask) >> 16U;
            data[ii + 2] = blue;
            data[ii + 1] = green;
            data[ii + 0] = red;
            ii += 3;
        }
    }
    return data;
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
                (unsigned char*)avaliable_targets,
                LENGTH(avaliable_targets)
            );
        } else if (request.target == atoms[A_ImagePng]) {
            trace("requested image/png");
            unsigned char* rgb_data = ximage_to_rgb(
                ctx->sel_buf.im,
                ctx->sel_buf.im->width,
                ctx->sel_buf.im->height
            );
            i32 png_data_size = NIL;
            stbi_uc* png_data = stbi_write_png_to_mem(
                rgb_data,
                0,
                ctx->sel_buf.im->width,
                ctx->sel_buf.im->height,
                3,
                &png_data_size
            );
            if (png_data == NULL) {
                die("stbi: %s", stbi_failure_reason());
            }

            XChangeProperty(
                request.display,
                request.requestor,
                request.property,
                request.target,
                8,
                PropModeReplace,
                png_data,
                png_data_size
            );

            free(rgb_data);
            stbi_image_free(png_data);
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
        Atom* data = NULL;
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
            (unsigned char**)&data
        );

        if (selection.target == atoms[A_Targets]) {
            for (u32 i = 0; i < count; ++i) {
                Atom li = data[i];
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

        if (data) {
            XFree(data);
        }
    }
    return True;
}

Bool client_message_hdlr(struct Ctx* ctx, XEvent* event) {
    // close window on request
    return False;
}

void cleanup(struct Ctx* ctx) {
    historyarr_clear(ctx->dc.dp, &ctx->hist_next);
    historyarr_clear(ctx->dc.dp, &ctx->hist_prev);
    for (i32 i = 0; i < TCS_NUM; ++i) {
        arrfree(ctx->tcs[i].sdata.colors_argb);
    }
    arrfree(ctx->tcs);
    XftFontClose(ctx->dc.dp, ctx->dc.fnt.xfont);
    XFreeGC(ctx->dc.dp, ctx->dc.gc);
    XFreeGC(ctx->dc.dp, ctx->dc.screen_gc);
    XDestroyImage(ctx->dc.cv.im);
    XdbeDeallocateBackBufferName(ctx->dc.dp, ctx->dc.back_buffer);
    if (ctx->sel_buf.im != NULL) {
        XDestroyImage(ctx->sel_buf.im);
    }
    for (u32 i = 0; i < I_Last; ++i) {
        if (images[i] != NULL) {
            XDestroyImage(images[i]);
        }
    }
    XDestroyWindow(ctx->dc.dp, ctx->dc.window);
}
