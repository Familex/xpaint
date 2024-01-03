#define _POSIX_C_SOURCE 200809L

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define XLeftMouseBtn   Button1
#define XMiddleMouseBtn Button2
#define XRightMouseBtn  Button3
#define MAX(A, B)       ((A) > (B) ? (A) : (B))
#define MIN(A, B)       ((A) < (B) ? (A) : (B))
#define CLAMP(X, L, H)  ((X < L) ? L : (X > H) ? H : X)
#define LENGTH(X)       (sizeof X / sizeof X[0])
#define PI              3.141
// default value for signed integers
#define NIL             -1

#define HAS_SELECTION(p_ctx) \
    p_ctx->tc.type == Tool_Selection && p_ctx->tc.data.sel.ex != NIL \
        && p_ctx->tc.data.sel.ey != NIL && p_ctx->tc.data.sel.bx != NIL \
        && p_ctx->tc.data.sel.by != NIL

enum {
    A_Clipboard,
    A_Targets,
    A_Utf8string,
    A_ImagePng,
    A_Last,
};

enum {
    I_Select,
    I_Pencil,
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
        Drawable screen;
        XVisualInfo vinfo;
        GC gc;
        GC screen_gc;
        Window window;
        u32 width;
        u32 height;
        struct Canvas {
            Pixmap pm;
            u32 width;
            u32 height;
        } cv;
    } dc;
    struct ToolCtx {
        void (*on_press)(struct DrawCtx*, struct ToolCtx*, XButtonPressedEvent const*);
        void (*on_release)(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
        void (*on_drag)(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);
        // static zero terminated string pointer
        char* ssz_tool_name;
        i32 prev_x;
        i32 prev_y;
        Bool is_holding;
        Bool is_dragging;
        enum ToolType {
            Tool_Selection,
            Tool_Pencil,
        } type;
        union ToolData {
            struct SelectionData {
                // selection begin
                i32 by, bx;
                // selection end
                i32 ey, ex;
            } sel;
            struct PencilData {
                u32 line_r;
                u32 col_rgb;
            } pencil;
        } data;
    } tc;
    struct History {
        struct Canvas cv;
        struct ToolCtx tc;
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
};

static void die(char const*, ...);
static void trace(char const*, ...);

static Pair point_from_cv_to_scr(i32, i32, struct DrawCtx const*);
static Pair point_from_scr_to_cv(i32, i32, struct DrawCtx const*);

static XImage* read_png_file(struct DrawCtx const*, char const*);

static void init_sel_circ_tools(struct SelectionCircle*, i32, i32);
static void free_sel_circ(struct SelectionCircle*);
static i32 current_sel_circ_item(struct SelectionCircle const*, i32, i32);
static struct SelectonCircleDims
get_curr_sel_dims(struct SelectionCircle const*);

static void set_current_tool_selection(struct ToolCtx*);
static void set_current_tool_pencil(struct ToolCtx*);

static void
tool_selection_on_press(struct DrawCtx*, struct ToolCtx*, XButtonPressedEvent const*);
static void
tool_selection_on_release(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
static void
tool_selection_on_drag(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);
static void
tool_pencil_on_release(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
static void
tool_pencil_on_drag(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);

static Bool history_move(struct Ctx*, Bool forward);
static Bool history_push(struct History**, struct Ctx*);

static void historyarr_clear(Display*, struct History**);
static void canvas_clear(Display*, struct Canvas*);
static void tool_ctx_clear(struct ToolCtx*);

static void
draw_selection_circle(struct DrawCtx*, struct SelectionCircle const*, i32, i32);
static void clear_selection_circle(struct DrawCtx*, struct SelectionCircle*);
static void update_screen(struct Ctx*);

// static void resize_canvas(struct DrawCtx*, i32, i32);

static struct Ctx setup(Display*);
static void run(struct Ctx*);
static Bool button_press_hdlr(struct Ctx*, XEvent*);
static Bool button_release_hdlr(struct Ctx*, XEvent*);
static Bool destroy_notify_hdlr(struct Ctx*, XEvent*);
static Bool expose_hdlr(struct Ctx*, XEvent*);
static Bool key_press_hdlr(struct Ctx*, XEvent*);
static Bool mapping_notify_hdlr(struct Ctx*, XEvent*);
static Bool motion_notify_hdlr(struct Ctx*, XEvent*);
static Bool configure_notify_hdlr(struct Ctx*, XEvent*);
static Bool selection_request_hdlr(struct Ctx*, XEvent*);
static Bool selection_notify_hdlr(struct Ctx*, XEvent*);
static Bool client_message_hdlr(struct Ctx*, XEvent*);
static void cleanup(struct Ctx*);

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

Pair point_from_cv_to_scr(i32 x, i32 y, struct DrawCtx const* dc) {
    return (Pair) {
        .x = x + ((i32)dc->width - (i32)dc->cv.width) / 2,
        .y = y + ((i32)dc->height - (i32)dc->cv.height) / 2,
    };
}

Pair point_from_scr_to_cv(i32 x, i32 y, struct DrawCtx const* dc) {
    return (Pair) {
        .x = x - ((i32)dc->width - (i32)dc->cv.width) / 2,
        .y = y - ((i32)dc->height - (i32)dc->cv.height) / 2,
    };
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
        32,
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
            };
            break;
        case Tool_Pencil:
            new_tc.on_release = &tool_pencil_on_release;
            new_tc.on_drag = &tool_pencil_on_drag;
            new_tc.ssz_tool_name = "pencil";
            new_tc.data.pencil = (struct PencilData) {
                .line_r = 5,
                .col_rgb = 0x00FF00,
            };
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

void tool_selection_on_press(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XButtonPressedEvent const* event
) {
    assert(tc->type == Tool_Selection);
    if (event->button == XLeftMouseBtn) {
        struct SelectionData* sd = &tc->data.sel;
        Pair pointer = point_from_scr_to_cv(event->x, event->y, dc);
        sd->bx = CLAMP(pointer.x, 0, dc->cv.width);
        sd->by = CLAMP(pointer.y, 0, dc->cv.height);
        sd->ex = NIL;
        sd->ey = NIL;
    }
}

void tool_selection_on_release(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XButtonReleasedEvent const* event
) {
    if (event->button == XLeftMouseBtn) {
        if (tc->is_dragging) {
            // select area
            XSetSelectionOwner(dc->dp, XA_PRIMARY, dc->window, CurrentTime);
            trace("clipboard owned");
        } else {
            // unselect area
            struct SelectionData* sd = &tc->data.sel;
            sd->bx = NIL;
            sd->ey = NIL;
            sd->by = NIL;
            sd->ex = NIL;
            XSetSelectionOwner(dc->dp, XA_PRIMARY, None, CurrentTime);
            trace("clipboard released");
        }
    }
}

void tool_selection_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XMotionEvent const* event
) {
    if (tc->is_holding) {
        Pair pointer = point_from_scr_to_cv(event->x, event->y, dc);
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

    if (!tc->is_dragging) {
        Pair pointer = point_from_scr_to_cv(event->x, event->y, dc);
        XSetForeground(dc->dp, dc->gc, tc->data.pencil.col_rgb);
        XFillArc(
            dc->dp,
            dc->cv.pm,
            dc->gc,
            pointer.x - tc->data.pencil.line_r / 2,
            pointer.y - tc->data.pencil.line_r / 2,
            tc->data.pencil.line_r,
            tc->data.pencil.line_r,
            0,
            360 * 64
        );
    }
}

void tool_pencil_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tc,
    XMotionEvent const* event
) {
    assert(tc->type == Tool_Pencil);
    assert(tc->is_holding);

    Pair pointer = point_from_scr_to_cv(event->x, event->y, dc);
    Pair prev_pointer = point_from_scr_to_cv(tc->prev_x, tc->prev_y, dc);
    XSetForeground(dc->dp, dc->gc, tc->data.pencil.col_rgb);
    XSetLineAttributes(
        dc->dp,
        dc->gc,
        tc->data.pencil.line_r,
        LineSolid,
        CapRound,  // fills gaps in lines
        JoinMiter  // FIXME what is it
    );
    XDrawLine(
        dc->dp,
        dc->cv.pm,
        dc->gc,
        prev_pointer.x,
        prev_pointer.y,
        pointer.x,
        pointer.y
    );
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
    tool_ctx_clear(&ctx->tc);

    // apply history
    ctx->dc.cv = curr.cv;
    ctx->tc = curr.tc;

    return True;
}

Bool history_push(struct History** hist, struct Ctx* ctx) {
    trace("xpaint: history push");

    struct History new_item = {
        .cv = ctx->dc.cv,
        .tc = ctx->tc,
    };

    new_item.cv.pm = XCreatePixmap(
        ctx->dc.dp,
        ctx->dc.window,
        ctx->dc.width,
        ctx->dc.height,
        ctx->dc.vinfo.depth
    );
    XCopyArea(
        ctx->dc.dp,
        ctx->dc.cv.pm,
        new_item.cv.pm,
        ctx->dc.gc,
        0,
        0,
        ctx->dc.width,
        ctx->dc.height,
        0,
        0
    );

    arrpush(*hist, new_item);

    return True;
}

void historyarr_clear(Display* dp, struct History** hist) {
    for (u32 i = 0; i < arrlenu(*hist); ++i) {
        struct History* h = &(*hist)[i];
        canvas_clear(dp, &h->cv);
        tool_ctx_clear(&h->tc);
    }
    arrfree(*hist);
}

void canvas_clear(Display* dp, struct Canvas* cv) {
    XFreePixmap(dp, cv->pm);
}

void tool_ctx_clear(struct ToolCtx* tc) {
    // do nothing
}

void init_sel_circ_tools(struct SelectionCircle* sc, i32 x, i32 y) {
    static struct Item tools[] = {
        [0] = {.on_select = &set_current_tool_selection, .hicon = I_Select},
        [1] = {.on_select = &set_current_tool_pencil, .hicon = I_Pencil},
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

    if (pointer_r <= sel_rect.outer.r && pointer_r >= sel_rect.inner.r) {
        // FIXME do it right.
        double angle = -atan(pointer_y_rel * 1.0 / pointer_x_rel) / PI * 180;
        if (pointer_x_rel < 0) {
            angle += 180;
        } else if (pointer_y_rel >= 0) {
            angle += 360;
        }

        return angle / segment_deg;
    } else {
        return NIL;
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
    XSetForeground(dc->dp, dc->screen_gc, SELECTION_CIRCLE.background_rgb);
    XFillArc(
        dc->dp,
        dc->screen,
        dc->screen_gc,
        sel_rect.outer.x,
        sel_rect.outer.y,
        sel_rect.outer.r * 2,
        sel_rect.outer.r * 2,
        0,
        360 * 64
    );

    XSetForeground(dc->dp, dc->screen_gc, SELECTION_CIRCLE.line_col_rgb);
    XDrawArc(
        dc->dp,
        dc->screen,
        dc->screen_gc,
        sel_rect.inner.x,
        sel_rect.inner.y,
        sel_rect.inner.r * 2,
        sel_rect.inner.r * 2,
        0,
        360 * 64
    );

    XDrawArc(
        dc->dp,
        dc->screen,
        dc->screen_gc,
        sel_rect.outer.x,
        sel_rect.outer.y,
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
                    dc->screen,
                    dc->screen_gc,
                    sc->x + cos(segment_rad * line_num) * sel_rect.inner.r,
                    sc->y + sin(segment_rad * line_num) * sel_rect.inner.r,
                    sc->x + cos(segment_rad * line_num) * sel_rect.outer.r,
                    sc->y + sin(segment_rad * line_num) * sel_rect.outer.r
                );
            }
        }

        for (u32 item = 0; item < sc->item_count; ++item) {
            XImage* image = images[sc->items[item].hicon];
            assert(image != NULL);

            // FIXME client-side transparency?
            XPutImage(
                dc->dp,
                dc->screen,
                dc->screen_gc,
                image,
                0,
                0,
                sc->x
                    + cos(segment_rad * (item - 0.5))
                        * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5)
                    - image->width / 2.0,
                sc->y
                    + sin(segment_rad * (item - 0.5))
                        * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5)
                    - image->height / 2.0,
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
                SELECTION_CIRCLE.active_background_rgb
            );
            XFillArc(
                dc->dp,
                dc->screen,
                dc->screen_gc,
                sel_rect.outer.x,
                sel_rect.outer.y,
                sel_rect.outer.r * 2,
                sel_rect.outer.r * 2,
                (current_item * segment_deg) * 64,
                segment_deg * 64
            );
            XSetForeground(
                dc->dp,
                dc->screen_gc,
                SELECTION_CIRCLE.active_inner_background_rgb
            );
            XFillArc(
                dc->dp,
                dc->screen,
                dc->screen_gc,
                sel_rect.inner.x,
                sel_rect.inner.y,
                sel_rect.inner.r * 2,
                sel_rect.inner.r * 2,
                (current_item * segment_deg) * 64,
                segment_deg * 64
            );
        }
    }
}

void clear_selection_circle(struct DrawCtx* dc, struct SelectionCircle* sc) {
    struct SelectonCircleDims sel_rect = get_curr_sel_dims(sc);

    XClearArea(
        dc->dp,
        dc->screen,
        sel_rect.outer.x - 1,
        sel_rect.outer.y - 1,
        sel_rect.outer.r * 2 + 2,
        sel_rect.outer.r * 2 + 2,
        True  // Expose to draw background
    );
}

void update_screen(struct Ctx* ctx) {
    Pair cv_pivot = point_from_cv_to_scr(0, 0, &ctx->dc);

    XSetForeground(ctx->dc.dp, ctx->dc.screen_gc, WINDOW.background_rgb);
    XFillRectangle(
        ctx->dc.dp,
        ctx->dc.window,
        ctx->dc.screen_gc,
        0,
        0,
        ctx->dc.width,
        ctx->dc.height
    );
    XCopyArea(
        ctx->dc.dp,
        ctx->dc.cv.pm,
        ctx->dc.screen,
        ctx->dc.gc,
        0,
        0,
        ctx->dc.cv.width,
        ctx->dc.cv.height,
        cv_pivot.x,
        cv_pivot.y
    );
    /* current selection */ {
        if (HAS_SELECTION(ctx)) {
            struct SelectionData sd = ctx->tc.data.sel;
            XSetLineAttributes(
                ctx->dc.dp,
                ctx->dc.screen_gc,
                SELECTION_TOOL.line_w,
                SELECTION_TOOL.line_style,
                SELECTION_TOOL.cap_style,
                SELECTION_TOOL.join_style
            );
            u32 x = MIN(sd.bx, sd.ex);
            u32 y = MIN(sd.by, sd.ey);
            u32 w = MAX(sd.bx, sd.ex) - x;
            u32 h = MAX(sd.by, sd.ey) - y;
            XDrawRectangle(
                ctx->dc.dp,
                ctx->dc.screen,
                ctx->dc.screen_gc,
                cv_pivot.x + x,
                cv_pivot.y + y,
                w,
                h
            );
        }
    }
    /* statusline */ {
        struct DrawCtx* dc = &ctx->dc;
        struct ToolCtx* tc = &ctx->tc;
        XSetForeground(dc->dp, dc->screen_gc, STATUSLINE.background_rgb);
        XFillRectangle(
            dc->dp,
            dc->screen,
            dc->screen_gc,
            0,
            dc->height - STATUSLINE.height_px,
            dc->width,
            STATUSLINE.height_px
        );
        /* captions */ {
            XSetBackground(dc->dp, dc->screen_gc, STATUSLINE.background_rgb);
            XSetForeground(dc->dp, dc->screen_gc, STATUSLINE.font_rgb);
            if (tc->ssz_tool_name) {
                XDrawImageString(
                    dc->dp,
                    dc->screen,
                    dc->screen_gc,
                    0,
                    dc->height,
                    tc->ssz_tool_name,
                    strlen(tc->ssz_tool_name)
                );
            }
            if (tc->type == Tool_Pencil) {
                u32 const col_value_size = 1 + 6;
                char col_value[col_value_size + 2];  // FIXME ?
                sprintf(col_value, "#%06X", tc->data.pencil.col_rgb);
                XDrawImageString(
                    dc->dp,
                    dc->screen,
                    dc->screen_gc,
                    dc->width - 50,  // FIXME
                    dc->height,
                    col_value,
                    col_value_size
                );
            }
        }
    }
}

void resize_canvas(struct DrawCtx* dc, i32 new_width, i32 new_height) {
    Pixmap new_pm = XCreatePixmap(
        dc->dp,
        dc->window,
        new_width,
        new_height,
        dc->vinfo.depth
    );

    XSetForeground(dc->dp, dc->gc, CANVAS.background_rgb);
    XFillRectangle(dc->dp, new_pm, dc->gc, 0, 0, new_width, new_height);

    XCopyArea(
        dc->dp,
        dc->cv.pm,
        new_pm,
        dc->gc,
        0,
        0,
        dc->cv.width,
        dc->cv.height,
        0,
        0
    );
    XFreePixmap(dc->dp, dc->cv.pm);

    dc->cv.pm = new_pm;
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
    struct Ctx ctx = {
        .dc.dp = dp,
        .dc.width = CANVAS.default_width,
        .dc.height = CANVAS.default_height,
        .hist_next = NULL,
        .hist_prev = NULL,
        .sel_buf.im = NULL,
    };

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
           .background_pixel = 0x80800000,
           .event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask
               | ExposureMask | PointerMotionMask | StructureNotifyMask}
    );
    ctx.dc.screen = ctx.dc.window;
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

    /* turn on protocol support */ {
        Atom wm_delete_window = XInternAtom(dp, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dp, ctx.dc.window, &wm_delete_window, 1);
    }

    /* static images */ {
        for (i32 i = 0; i < I_Last; ++i) {
            images[i] = read_png_file(
                &ctx.dc,
                i == I_Select       ? "./res/tool-select.png"
                    : i == I_Pencil ? "./res/tool-pencil.png"
                                    : "null"
            );
        }
    }

    /* canvas */ {
        ctx.dc.cv.width = CANVAS.default_width;
        ctx.dc.cv.height = CANVAS.default_height;
        ctx.dc.cv.pm = XCreatePixmap(
            dp,
            ctx.dc.window,
            ctx.dc.cv.width,
            ctx.dc.cv.height,
            ctx.dc.vinfo.depth
        );
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
        XSetForeground(dp, ctx.dc.gc, CANVAS.background_rgb);
        XFillRectangle(
            dp,
            ctx.dc.cv.pm,
            ctx.dc.gc,
            0,
            0,
            ctx.dc.cv.width,
            ctx.dc.cv.height
        );
    }

    set_current_tool_selection(&ctx.tc);
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
    if (ctx->tc.on_press) {
        ctx->tc.on_press(&ctx->dc, &ctx->tc, e);
        update_screen(ctx);
    }
    if (e->button == XLeftMouseBtn) {
        // next history invalidated after user action
        historyarr_clear(ctx->dc.dp, &ctx->hist_next);
        history_push(&ctx->hist_prev, ctx);
        ctx->tc.is_holding = True;
    }

    return True;
}

Bool button_release_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    if (e->button == XRightMouseBtn) {
        i32 const selected_item = current_sel_circ_item(&ctx->sc, e->x, e->y);
        if (selected_item != NIL && ctx->sc.items[selected_item].on_select) {
            ctx->sc.items[selected_item].on_select(&ctx->tc);
        }
        free_sel_circ(&ctx->sc);
        clear_selection_circle(&ctx->dc, &ctx->sc);
    }
    if (ctx->tc.on_release) {
        ctx->tc.on_release(&ctx->dc, &ctx->tc, e);
        update_screen(ctx);
    }
    ctx->tc.is_holding = False;
    ctx->tc.is_dragging = False;

    return True;
}

Bool destroy_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    return True;
}

Bool expose_hdlr(struct Ctx* ctx, XEvent* event) {
    update_screen(ctx);
    return True;
}

Bool key_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XKeyPressedEvent e = event->xkey;
    if (e.type == KeyRelease) {
        return True;
    }

    KeySym const key_sym = XLookupKeysym(&e, 0);
    switch (key_sym) {
        case XK_q:
            return False;
        case XK_u:
        case XK_z:
            if (e.state & ControlMask) {
                if (!history_move(ctx, !(e.state & ShiftMask))) {
                    trace("xpaint: can't undo/revert history");
                }
                update_screen(ctx);
            }
            break;
        case XK_c:
            if (e.state & ControlMask && HAS_SELECTION(ctx)) {
                XSetSelectionOwner(
                    ctx->dc.dp,
                    atoms[A_Clipboard],
                    ctx->dc.window,
                    CurrentTime
                );
                i32 x = MIN(ctx->tc.data.sel.bx, ctx->tc.data.sel.ex);
                i32 y = MIN(ctx->tc.data.sel.by, ctx->tc.data.sel.ey);
                u32 width = MAX(ctx->tc.data.sel.ex, ctx->tc.data.sel.bx) - x;
                u32 height = MAX(ctx->tc.data.sel.ey, ctx->tc.data.sel.by) - y;
                if (ctx->sel_buf.im != NULL) {
                    XDestroyImage(ctx->sel_buf.im);
                }
                ctx->sel_buf.im = XGetImage(
                    ctx->dc.dp,
                    ctx->dc.cv.pm,
                    x,
                    y,
                    width,
                    height,
                    AllPlanes,
                    ZPixmap
                );
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
            }
            break;
        case XK_Up:
        case XK_Down:
            if (ctx->tc.type == Tool_Pencil) {
                u32 colors[] = {
                    0x000000,
                    0xFF0000,
                    0x00FF00,
                    0x0000FF,
                    0xFFFF00,
                    0x00FFFF,
                    0xFF00FF,
                    0xFFFFFF,
                };
                u32 col_num = LENGTH(colors);
                i32 curr_col = 0;  // first by default
                for (i32 i = 0; i < col_num; ++i) {
                    if (ctx->tc.data.pencil.col_rgb == colors[i]) {
                        curr_col = i;
                        break;
                    }
                }
                ctx->tc.data.pencil.col_rgb =
                    colors[(curr_col + (key_sym == XK_Up ? 1 : -1)) % col_num];
                update_screen(ctx);
            }
            break;
    }
    return True;
}

Bool mapping_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XRefreshKeyboardMapping(&event->xmapping);
    return True;
}

Bool motion_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    XMotionEvent* e = (XMotionEvent*)event;

    if (ctx->tc.is_holding) {
        ctx->tc.is_dragging = True;
        if (ctx->tc.on_drag) {
            ctx->tc.on_drag(&ctx->dc, &ctx->tc, e);
            update_screen(ctx);
        }
    }

    draw_selection_circle(&ctx->dc, &ctx->sc, e->x, e->y);

    ctx->tc.prev_x = e->x;
    ctx->tc.prev_y = e->y;

    return True;
}

Bool configure_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    ctx->dc.width = event->xconfigure.width;
    ctx->dc.height = event->xconfigure.height;

    return True;
}

// FIXME remove
static unsigned char* ximage_to_rgb(XImage* image, i32 const w, i32 const h) {
    unsigned char* data = (unsigned char*)malloc(w * h * 3);
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
            unsigned char green = (pixel & green_mask) >> 8;
            unsigned char red = (pixel & red_mask) >> 16;
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
        Atom actual_type;
        i32 actual_format;
        u64 bytes_after;
        Atom* data;
        u64 count;
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
            if (target != None)
                XConvertSelection(
                    ctx->dc.dp,
                    atoms[A_Clipboard],
                    target,
                    atoms[A_Clipboard],
                    ctx->dc.window,
                    CurrentTime
                );
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
    XFreeGC(ctx->dc.dp, ctx->dc.gc);
    XFreeGC(ctx->dc.dp, ctx->dc.screen_gc);
    XFreePixmap(ctx->dc.dp, ctx->dc.cv.pm);
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
