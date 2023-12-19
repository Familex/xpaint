#define _POSIX_C_SOURCE 200809L

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_DS_IMPLEMENTATION
#include "lib/stb_ds.h"
#undef STB_DS_IMPLEMENTATION

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define PI        3.141

#include "config.h"

struct SelectonCircleDims {
    struct CircleDims {
        unsigned int x;
        unsigned int y;
        unsigned int r;
    } outer, inner;
};

struct Ctx {
    struct DrawCtx {
        Display* dp;
        // FIXME rename to screen
        Drawable screen;
        GC gc;
        GC screen_gc;
        Window window;
        unsigned int width;
        unsigned int height;
        struct Canvas {
            Pixmap pm;
            unsigned int width;
            unsigned int height;
        } cv;
    } dc;
    struct ToolCtx {
        void (*on_click)(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
        void (*on_drag)(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);
        // static zero terminated string pointer
        char* ssz_tool_name;
        int prev_x;
        int prev_y;
        Bool is_holding;
        Bool is_dragging;
        enum ToolType {
            Tool_Selection,
            Tool_Pencil,
        } type;
    } tc;
    struct History {
        struct Canvas cv;
        struct ToolCtx tc;
    } *hist_prev, *hist_next;
    struct SelectionCircle {
        Bool is_active;
        int x;
        int y;
        unsigned int item_count;
        struct Item {
            void (*on_select)(struct ToolCtx*);
            int hicon;
        }* items;
    } sc;
};

static void die(char const* errstr, ...);

static void init_sel_circ_tools(struct SelectionCircle*, int, int);
static void free_sel_circ(struct SelectionCircle*);
static int current_sel_circ_item(struct SelectionCircle const*, int, int);
static struct SelectonCircleDims
get_curr_sel_dims(struct SelectionCircle const*);

static void set_current_tool_selection(struct ToolCtx*);
static void set_current_tool_pencil(struct ToolCtx*);

static void
tool_selection_on_click(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
static void
tool_pencil_on_click(struct DrawCtx*, struct ToolCtx*, XButtonReleasedEvent const*);
static void
tool_pencil_on_drag(struct DrawCtx*, struct ToolCtx*, XMotionEvent const*);

static Bool history_move(struct Ctx*, Bool forward);
static Bool history_push(struct History**, struct Ctx*);

static void historyarr_clear(Display*, struct History**);
static void history_clear(Display*, struct History*);
static void canvas_clear(Display*, struct Canvas*);
static void tool_ctx_clear(struct ToolCtx*);

static void
draw_selection_circle(struct DrawCtx*, struct SelectionCircle const*, int, int);
static void clear_selection_circle(struct DrawCtx*, struct SelectionCircle*);
static void draw_statusline(struct DrawCtx*, struct ToolCtx const*);
static void update_screen(struct DrawCtx*, struct ToolCtx const*);

static void resize_canvas(struct DrawCtx*, int, int);

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
static void cleanup(struct Ctx*);

int main(int argc, char** argv) {
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        die("cannot open display\n");
    }

    struct Ctx ctx = setup(display);
    run(&ctx);
    cleanup(&ctx);
    XCloseDisplay(display);

    return 0;
}

void die(char const* errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

struct SelectonCircleDims
get_curr_sel_dims(struct SelectionCircle const* sel_circ) {
    struct SelectonCircleDims result = {
        .outer =
            {
                .x = sel_circ->x - SELECTION_OUTER_RADIUS_PX,
                .y = sel_circ->y - SELECTION_OUTER_RADIUS_PX,
                .r = SELECTION_OUTER_RADIUS_PX,
            },
        .inner =
            {
                .x = sel_circ->x - SELECTION_INNER_RADIUS_PX,
                .y = sel_circ->y - SELECTION_INNER_RADIUS_PX,
                .r = SELECTION_INNER_RADIUS_PX,
            },
    };

    return result;
}

void set_current_tool_selection(struct ToolCtx* tool_ctx) {
    *tool_ctx = (const struct ToolCtx) {0};
    tool_ctx->on_click = &tool_selection_on_click;
    tool_ctx->ssz_tool_name = "selection";
    tool_ctx->type = Tool_Selection;
}

void set_current_tool_pencil(struct ToolCtx* tool_ctx) {
    *tool_ctx = (const struct ToolCtx) {0};
    tool_ctx->on_click = &tool_pencil_on_click;
    tool_ctx->on_drag = &tool_pencil_on_drag;
    tool_ctx->ssz_tool_name = "pencil";
    tool_ctx->type = Tool_Pencil;
}

void tool_selection_on_click(
    struct DrawCtx* dc,
    struct ToolCtx* tool,
    XButtonReleasedEvent const* event
) {
    // FIXME
    puts("selection action");
}

void tool_pencil_on_click(
    struct DrawCtx* dc,
    struct ToolCtx* tool,
    XButtonReleasedEvent const* event
) {
    puts("pencil action");
    // FIXME use XDrawArc and XFillArc
    XDrawPoint(dc->dp, dc->cv.pm, dc->gc, event->x, event->y);
}

void tool_pencil_on_drag(
    struct DrawCtx* dc,
    struct ToolCtx* tool,
    XMotionEvent const* event
) {
    assert(tool->is_holding);
    XSetForeground(dc->dp, dc->gc, 0x00FF00);
    XDrawLine(
        dc->dp,
        dc->cv.pm,
        dc->gc,
        tool->prev_x,
        tool->prev_y,
        event->x,
        event->y
    );
}

///  forward? current state into hist_next; pop state from hist_prev.
/// !forward? current state into hist_prev; pop state from hist_next.
Bool history_move(struct Ctx* ctx, Bool forward) {
    printf("history move %d\n", forward);

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
    puts("xpaint: history push");

    struct History new_item = {
        .cv = ctx->dc.cv,
        .tc = ctx->tc,
    };

    new_item.cv.pm = XCreatePixmap(
        ctx->dc.dp,
        ctx->dc.screen,
        ctx->dc.width,
        ctx->dc.height,
        // FIXME
        DefaultDepth(ctx->dc.dp, 0)
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
    for (unsigned int i = 0; i < arrlenu(*hist); ++i) {
        history_clear(dp, &(*hist)[i]);
    }
    arrfree(*hist);
}

// FIXME may be removed?
void history_clear(Display* dp, struct History* hist) {
    canvas_clear(dp, &hist->cv);
    tool_ctx_clear(&hist->tc);
}

void canvas_clear(Display* dp, struct Canvas* cv) {
    XFreePixmap(dp, cv->pm);
}

void tool_ctx_clear(struct ToolCtx* tc) {
    // do nothing
}

void init_sel_circ_tools(struct SelectionCircle* sc, int x, int y) {
    static struct Item tools[] = {
        [0] = {&set_current_tool_selection, 0},
        [1] = {&set_current_tool_pencil, 0},
    };

    sc->is_active = True;
    sc->x = x;
    sc->y = y;
    sc->item_count = LENGTH(tools);
    sc->items = tools;
}

int current_sel_circ_item(struct SelectionCircle const* sc, int x, int y) {
    struct SelectonCircleDims sel_rect = get_curr_sel_dims(sc);
    int const pointer_x_rel = x - sc->x;
    int const pointer_y_rel = y - sc->y;
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
        return -1;
    }
}

void draw_selection_circle(
    struct DrawCtx* dc,
    struct SelectionCircle const* sc,
    int const pointer_x,
    int const pointer_y
) {
    if (!sc->is_active) {
        return;
    }

    struct SelectonCircleDims sel_rect = get_curr_sel_dims(sc);

    XSetForeground(dc->dp, dc->screen_gc, 0xFFFFFF);
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

    XSetForeground(dc->dp, dc->screen_gc, 0x000000);
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
            for (unsigned int line_num = 0; line_num < sc->item_count;
                 ++line_num) {
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

        for (unsigned int image_num = 0; image_num < sc->item_count;
             ++image_num) {
            XSetForeground(dc->dp, dc->screen_gc, 0xFFFFFF);
            XDrawImageString(
                dc->dp,
                dc->screen,
                dc->screen_gc,
                sc->x
                    + cos(segment_rad * (image_num + 0.5))
                        * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5),
                sc->y
                    + sin(segment_rad * (image_num + 0.5))
                        * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5),
                "T",
                1
            );
        }

        // pointer
        int const current_item =
            current_sel_circ_item(sc, pointer_x, pointer_y);
        if (current_item != -1) {
            XSetForeground(dc->dp, dc->screen_gc, 0x888888);
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
            XSetForeground(dc->dp, dc->screen_gc, 0xAAAAAA);
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

void draw_statusline(struct DrawCtx* dc, struct ToolCtx const* tc) {
    XSetForeground(dc->dp, dc->screen_gc, STATUSLINE_BACKGROUND_RGB);
    XFillRectangle(
        dc->dp,
        dc->screen,
        dc->screen_gc,
        0,
        dc->height - STATUSLINE_HEIGHT_PX,
        dc->width,
        STATUSLINE_HEIGHT_PX
    );
    if (tc->ssz_tool_name) {
        XSetBackground(dc->dp, dc->screen_gc, STATUSLINE_BACKGROUND_RGB);
        XSetForeground(dc->dp, dc->screen_gc, STATUSLINE_FONT_RGB);
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
}

void update_screen(struct DrawCtx* dc, struct ToolCtx const* tc) {
    XCopyArea(
        dc->dp,
        dc->cv.pm,
        dc->screen,
        dc->gc,
        0,
        0,
        dc->cv.width,
        dc->cv.height,
        0,
        0
    );
    draw_statusline(dc, tc);
}

void resize_canvas(struct DrawCtx* dc, int new_width, int new_height) {
    Pixmap new_pm = XCreatePixmap(
        dc->dp,
        dc->window,
        new_width,
        new_height,
        // FIXME
        DefaultDepth(dc->dp, 0)
    );

    XSetForeground(dc->dp, dc->gc, CANVAS_BACKGROUND_RGB);
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
        .dc.width = 350,
        .dc.height = 250,
        .hist_next = NULL,
        .hist_prev = NULL,
    };

    int screen = DefaultScreen(dp);

    /* drawing contexts for an window */
    unsigned int foreground = BlackPixel(dp, screen);
    unsigned int background = WhitePixel(dp, screen);
    // FIXME check values
    XSizeHints hint = {
        .x = 200,
        .y = 300,
        .width = ctx.dc.width,
        .height = ctx.dc.height,
        .flags = PPosition | PSize,
    };

    /* create window */
    ctx.dc.window = XCreateSimpleWindow(
        dp,
        DefaultRootWindow(dp),
        hint.x,
        hint.y,
        hint.width,
        hint.height,
        5,
        foreground,
        background
    );
    ctx.dc.screen = ctx.dc.window;

    XSetStandardProperties(
        dp,
        ctx.dc.window,
        title,
        NULL,
        None,
        NULL,
        0,
        &hint
    );

    ctx.dc.screen_gc = XCreateGC(dp, ctx.dc.window, 0, 0);

    XSelectInput(
        dp,
        ctx.dc.window,
        ButtonPressMask | ButtonReleaseMask | KeyPressMask | ExposureMask
            | PointerMotionMask | StructureNotifyMask
    );

    /* canvas */ {
        ctx.dc.cv.width = ctx.dc.width;
        ctx.dc.cv.height = ctx.dc.height - STATUSLINE_HEIGHT_PX;
        ctx.dc.cv.pm = XCreatePixmap(
            dp,
            ctx.dc.window,
            ctx.dc.cv.width,
            ctx.dc.cv.height,
            // FIXME
            DefaultDepth(dp, 0)
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
        XSetForeground(dp, ctx.dc.gc, CANVAS_BACKGROUND_RGB);
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

    /* show up window */
    XMapRaised(dp, ctx.dc.window);

    return ctx;
}

Bool button_press_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    if (e->button == Button3) {
        init_sel_circ_tools(&ctx->sc, e->x, e->y);
        draw_selection_circle(&ctx->dc, &ctx->sc, -1, -1);
    }
    if (e->button == Button1) {
        ctx->tc.is_holding = True;
    }

    return True;
}

Bool button_release_hdlr(struct Ctx* ctx, XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    if (e->button == Button3) {
        int const selected_item = current_sel_circ_item(&ctx->sc, e->x, e->y);
        if (selected_item != -1 && ctx->sc.items[selected_item].on_select) {
            ctx->sc.items[selected_item].on_select(&ctx->tc);
        }
        free_sel_circ(&ctx->sc);
        clear_selection_circle(&ctx->dc, &ctx->sc);
    } else if (ctx->tc.on_click) {
        ctx->tc.on_click(&ctx->dc, &ctx->tc, e);
        ctx->tc.is_holding = False;
        // next history invalidated after user action
        historyarr_clear(ctx->dc.dp, &ctx->hist_next);
        history_push(&ctx->hist_prev, ctx);
        update_screen(&ctx->dc, &ctx->tc);
    }

    return True;
}

Bool destroy_notify_hdlr(struct Ctx* ctx, XEvent* event) {
    return True;
}

Bool expose_hdlr(struct Ctx* ctx, XEvent* event) {
    update_screen(&ctx->dc, &ctx->tc);
    return True;
}

Bool key_press_hdlr(struct Ctx* ctx, XEvent* event) {
    static char text[10];

    if (XLookupString(&event->xkey, text, 10, NULL, NULL)) {
        switch (text[0]) {
            case 'q':
                return False;
            case 'u':
            case 'z':
                if (!history_move(ctx, True)) {
                    puts("xpaint: can't undo history");
                }
                update_screen(&ctx->dc, &ctx->tc);
                break;
            case 'U':
            case 'Z':
                if (!history_move(ctx, False)) {
                    puts("xpaint: can't revert history");
                }
                update_screen(&ctx->dc, &ctx->tc);
                break;
        }
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
            // FIXME move it to better place
            update_screen(&ctx->dc, &ctx->tc);
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

    unsigned int new_cv_width = ctx->dc.width;
    unsigned int new_cv_height = ctx->dc.height - STATUSLINE_HEIGHT_PX;

    if (ctx->dc.cv.width != new_cv_width
        || ctx->dc.cv.height != new_cv_height) {
        resize_canvas(&ctx->dc, new_cv_width, new_cv_height);
    }

    return True;
}

void cleanup(struct Ctx* ctx) {
    historyarr_clear(ctx->dc.dp, &ctx->hist_next);
    historyarr_clear(ctx->dc.dp, &ctx->hist_prev);
    XFreeGC(ctx->dc.dp, ctx->dc.gc);
    XFreeGC(ctx->dc.dp, ctx->dc.screen_gc);
    XFreePixmap(ctx->dc.dp, ctx->dc.cv.pm);
    XDestroyWindow(ctx->dc.dp, ctx->dc.window);
}
