#define _POSIX_C_SOURCE 200809L

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define PI        3.141

#include "config.h"

struct Item {
    void (*on_select)(void);
    int hicon;
};

struct ToolCtx {
    void (*on_click)(XButtonReleasedEvent*);
    void (*on_drag)(XMotionEvent*);
    // static zero terminated string pointer
    char* ssz_tool_name;
    int prev_x;
    int prev_y;
    Bool is_holding;
};

struct SelectonCircle {
    Bool is_active;
    int x;
    int y;
    unsigned int item_count;
    struct Item* items;
};

struct SelectonCircleDims {
    struct CircleDims {
        unsigned int x;
        unsigned int y;
        unsigned int r;
    } outer, inner;
};

struct Canvas {
    Pixmap pm;
    GC gc;
    int width;
    int height;
};

static void die(char const* errstr, ...);

static void init_sel_circ_tools(int, int);
static void free_sel_circ(void);
static int current_sel_circ_item(int, int);
static struct SelectonCircleDims get_curr_sel_dims(void);

static void set_current_tool_selection(void);
static void set_current_tool_pencil(void);

static void tool_selection_on_click(XButtonReleasedEvent*);
static void tool_pencil_on_click(XButtonReleasedEvent*);
static void tool_pencil_on_drag(XMotionEvent*);

static void draw_selection_circle(int, int);
static void clear_selection_circle(void);
static void update_screen();

static void resize_canvas(int, int);

static void setup(void);
static void run(void);
static void button_press_hdlr(XEvent*);
static void button_release_hdlr(XEvent*);
static void destroy_notify_hdlr(XEvent*);
static void expose_hdlr(XEvent*);
static void key_press_hdlr(XEvent*);
static void mapping_notify_hdlr(XEvent*);
static void motion_notify_hdlr(XEvent*);
static void configure_notify_hdlr(XEvent*);
static void cleanup(void);

/* globals */
static Bool done = False;
static Display* display;
static Drawable drawable;
static GC gc;
static Window window;
static void (*handler[LASTEvent])(XEvent*) = {
    [ButtonPress] = button_press_hdlr,
    [ButtonRelease] = button_release_hdlr,
    [DestroyNotify] = destroy_notify_hdlr,
    [Expose] = expose_hdlr,
    [KeyPress] = key_press_hdlr,
    [MappingNotify] = mapping_notify_hdlr,
    [MotionNotify] = motion_notify_hdlr,
    [ConfigureNotify] = configure_notify_hdlr,
};
static struct SelectonCircle sel_circ = {0};
static struct ToolCtx tool_ctx = {0};
static struct Canvas canvas = {0};

int main(int argc, char** argv) {
    if (!(display = XOpenDisplay(NULL))) {
        die("cannot open display\n");
    }
    setup();
    run();
    cleanup();
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

struct SelectonCircleDims get_curr_sel_dims(void) {
    struct SelectonCircleDims result = {
        .outer =
            {
                .x = sel_circ.x - SELECTION_OUTER_RADIUS_PX,
                .y = sel_circ.y - SELECTION_OUTER_RADIUS_PX,
                .r = SELECTION_OUTER_RADIUS_PX,
            },
        .inner =
            {
                .x = sel_circ.x - SELECTION_INNER_RADIUS_PX,
                .y = sel_circ.y - SELECTION_INNER_RADIUS_PX,
                .r = SELECTION_INNER_RADIUS_PX,
            },
    };

    return result;
}

void set_current_tool_selection(void) {
    tool_ctx = (const struct ToolCtx) {0};
    tool_ctx.on_click = &tool_selection_on_click;
    tool_ctx.ssz_tool_name = "selection";
}

void set_current_tool_pencil(void) {
    tool_ctx.on_click = &tool_pencil_on_click;
    tool_ctx.on_drag = &tool_pencil_on_drag;
    tool_ctx.ssz_tool_name = "pencil";
}

void tool_selection_on_click(XButtonReleasedEvent* event) {
    // FIXME
    puts("selection action");
}

void tool_pencil_on_click(XButtonReleasedEvent* event) {
    puts("pencil action");
    // FIXME use XDrawArc and XFillArc
    XDrawPoint(display, canvas.pm, canvas.gc, event->x, event->y);
}

void tool_pencil_on_drag(XMotionEvent* event) {
    if (tool_ctx.is_holding) {
        XSetForeground(display, canvas.gc, 0x00FF00);
        XDrawLine(
            display,
            canvas.pm,
            canvas.gc,
            tool_ctx.prev_x,
            tool_ctx.prev_y,
            event->x,
            event->y
        );
    }
}

void init_sel_circ_tools(int x, int y) {
    static struct Item tools[] = {
        [0] = {&set_current_tool_selection, 0},
        [1] = {&set_current_tool_pencil, 0},
    };

    sel_circ.is_active = True;
    sel_circ.x = x;
    sel_circ.y = y;
    sel_circ.item_count = LENGTH(tools);
    sel_circ.items = tools;
}

int current_sel_circ_item(int x, int y) {
    struct SelectonCircleDims sel_rect = get_curr_sel_dims();
    int const pointer_x_rel = x - sel_circ.x;
    int const pointer_y_rel = y - sel_circ.y;
    double const segment_rad = PI * 2 / MAX(1, sel_circ.item_count);
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

void draw_selection_circle(int const pointer_x, int const pointer_y) {
    if (!sel_circ.is_active) {
        return;
    }

    struct SelectonCircleDims sel_rect = get_curr_sel_dims();

    XSetForeground(display, gc, 0xFFFFFF);
    XFillArc(
        display,
        drawable,
        gc,
        sel_rect.outer.x,
        sel_rect.outer.y,
        sel_rect.outer.r * 2,
        sel_rect.outer.r * 2,
        0,
        360 * 64
    );

    XSetForeground(display, gc, 0x000000);
    XDrawArc(
        display,
        drawable,
        gc,
        sel_rect.inner.x,
        sel_rect.inner.y,
        sel_rect.inner.r * 2,
        sel_rect.inner.r * 2,
        0,
        360 * 64
    );

    XDrawArc(
        display,
        drawable,
        gc,
        sel_rect.outer.x,
        sel_rect.outer.y,
        sel_rect.outer.r * 2,
        sel_rect.outer.r * 2,
        0,
        360 * 64
    );

    {
        double const segment_rad = PI * 2 / MAX(1, sel_circ.item_count);
        double const segment_deg = segment_rad / PI * 180;

        if (sel_circ.item_count >= 2) {
            for (unsigned int line_num = 0; line_num < sel_circ.item_count;
                 ++line_num) {
                XDrawLine(
                    display,
                    drawable,
                    gc,
                    sel_circ.x + cos(segment_rad * line_num) * sel_rect.inner.r,
                    sel_circ.y + sin(segment_rad * line_num) * sel_rect.inner.r,
                    sel_circ.x + cos(segment_rad * line_num) * sel_rect.outer.r,
                    sel_circ.y + sin(segment_rad * line_num) * sel_rect.outer.r
                );
            }
        }

        for (unsigned int image_num = 0; image_num < sel_circ.item_count;
             ++image_num) {
            XDrawImageString(
                display,
                drawable,
                gc,
                sel_circ.x
                    + cos(segment_rad * (image_num + 0.5))
                        * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5),
                sel_circ.y
                    + sin(segment_rad * (image_num + 0.5))
                        * ((sel_rect.outer.r + sel_rect.inner.r) * 0.5),
                "T",
                1
            );
        }

        // pointer
        int const current_item = current_sel_circ_item(pointer_x, pointer_y);
        if (current_item != -1) {
            XSetForeground(display, gc, 0x888888);
            XFillArc(
                display,
                drawable,
                gc,
                sel_rect.outer.x,
                sel_rect.outer.y,
                sel_rect.outer.r * 2,
                sel_rect.outer.r * 2,
                (current_item * segment_deg) * 64,
                segment_deg * 64
            );
            XSetForeground(display, gc, 0xAAAAAA);
            XFillArc(
                display,
                drawable,
                gc,
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

void clear_selection_circle(void) {
    struct SelectonCircleDims sel_rect = get_curr_sel_dims();

    XClearArea(
        display,
        drawable,
        sel_rect.outer.x - 1,
        sel_rect.outer.y - 1,
        sel_rect.outer.r * 2 + 2,
        sel_rect.outer.r * 2 + 2,
        True  // Expose to draw background
    );
}

void update_screen(void) {
    XCopyArea(
        display,
        canvas.pm,
        drawable,
        canvas.gc,
        0,
        0,
        canvas.width,
        canvas.height,
        0,
        0
    );
}

void resize_canvas(int new_width, int new_height) {
    Pixmap new_pm = XCreatePixmap(
        display,
        window,
        new_width,
        new_height,
        // FIXME
        DefaultDepth(display, 0)
    );

    XSetForeground(display, canvas.gc, CANVAS_BACKGROUND_RGB);
    XFillRectangle(display, new_pm, canvas.gc, 0, 0, new_width, new_height);

    XCopyArea(
        display,
        canvas.pm,
        new_pm,
        canvas.gc,
        0,
        0,
        canvas.width,
        canvas.height,
        0,
        0
    );
    XFreePixmap(display, canvas.pm);

    canvas.pm = new_pm;
    canvas.width = new_width;
    canvas.height = new_height;
}

void free_sel_circ(void) {
    sel_circ.is_active = False;
}

void run(void) {
    XEvent event;

    XSync(display, False);
    while (!done && !XNextEvent(display, &event)) {
        if (handler[event.type]) {
            handler[event.type](&event);
        }
    }
}

void setup(void) {
    int screen = DefaultScreen(display);

    /* drawing contexts for an window */
    unsigned int myforeground = BlackPixel(display, screen);
    unsigned int mybackground = WhitePixel(display, screen);
    // FIXME check values
    XSizeHints hint = {
        .x = 200,
        .y = 300,
        .width = 350,
        .height = 250,
        .flags = PPosition | PSize,
    };

    /* create window */
    window = XCreateSimpleWindow(
        display,
        DefaultRootWindow(display),
        hint.x,
        hint.y,
        hint.width,
        hint.height,
        5,
        myforeground,
        mybackground
    );
    drawable = window;

    XSetStandardProperties(display, window, title, NULL, None, NULL, 0, &hint);

    /* graphics context */
    gc = XCreateGC(display, window, 0, 0);
    XSetBackground(display, gc, mybackground);
    XSetForeground(display, gc, myforeground);

    /* allow receiving mouse events */
    XSelectInput(
        display,
        window,
        ButtonPressMask | ButtonReleaseMask | KeyPressMask | ExposureMask
            | PointerMotionMask | StructureNotifyMask
    );

    /* canvas */ {
        canvas.width = hint.width;
        canvas.height = hint.height;
        canvas.pm = XCreatePixmap(
            display,
            window,
            canvas.width,
            canvas.height,
            // FIXME
            DefaultDepth(display, 0)
        );
        XGCValues canvas_gc_vals = {
            .line_style = LineSolid,
            .line_width = 5,
            .cap_style = CapButt,
            .fill_style = FillSolid
        };
        canvas.gc = XCreateGC(
            display,
            window,
            GCForeground | GCBackground | GCFillStyle | GCLineStyle
                | GCLineWidth | GCCapStyle | GCJoinStyle,
            &canvas_gc_vals
        );
        // initial canvas color
        XSetForeground(display, canvas.gc, CANVAS_BACKGROUND_RGB);
        XFillRectangle(
            display,
            canvas.pm,
            canvas.gc,
            0,
            0,
            canvas.width,
            canvas.height
        );
    }

    /* show up window */
    XMapRaised(display, window);
}

void button_press_hdlr(XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    if (e->button == Button3) {
        init_sel_circ_tools(e->x, e->y);
        draw_selection_circle(-1, -1);
    }
    if (e->button == Button1) {
        tool_ctx.is_holding = True;
    }
}

void button_release_hdlr(XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    if (e->button == Button3) {
        int const selected_item = current_sel_circ_item(e->x, e->y);
        if (selected_item != -1 && sel_circ.items[selected_item].on_select) {
            sel_circ.items[selected_item].on_select();
        }
        free_sel_circ();
        clear_selection_circle();
    } else if (tool_ctx.on_click) {
        tool_ctx.on_click(e);
        tool_ctx.is_holding = False;
        update_screen();
    }
}

void destroy_notify_hdlr(XEvent* event) {}

void expose_hdlr(XEvent* event) {
    update_screen();
}

void key_press_hdlr(XEvent* event) {
    static char text[10];
    KeySym key;

    int i = XLookupString(&event->xkey, text, 10, &key, 0);
    if (i == 1 && text[0] == 'q') {
        done = True;
    }
}

void mapping_notify_hdlr(XEvent* event) {
    XRefreshKeyboardMapping(&event->xmapping);
}

void motion_notify_hdlr(XEvent* event) {
    XMotionEvent* e = (XMotionEvent*)event;

    if (tool_ctx.on_drag) {
        tool_ctx.on_drag(e);
        // FIXME move it to better place
        update_screen();
    }

    draw_selection_circle(e->x, e->y);

    tool_ctx.prev_x = e->x;
    tool_ctx.prev_y = e->y;
}

void configure_notify_hdlr(XEvent* event) {
    if (event->xconfigure.width != canvas.width
        || event->xconfigure.height != canvas.height) {
        resize_canvas(event->xconfigure.width, event->xconfigure.height);
    }
}

void cleanup(void) {
    XFreeGC(display, canvas.gc);
    XFreePixmap(display, canvas.pm);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
}
