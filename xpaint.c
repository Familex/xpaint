#define _POSIX_C_SOURCE 200809L

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <assert.h>
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
    void (*on_select)();
    int hicon;
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

static void die(char const* errstr, ...);

static void init_sel_circ_instruments(int, int);
static void free_sel_circ();

static struct SelectonCircleDims get_curr_sel_dims();

static void draw_selection_circle(int, int);
static void clear_selection_circle();

static void setup();
static void run();
static void button_press_hdlr(XEvent*);
static void button_release_hdlr(XEvent*);
static void destroy_notify_hdlr(XEvent*);
static void expose_hdlr(XEvent*);
static void key_press_hdlr(XEvent*);
static void mapping_notify_hdlr(XEvent*);
static void motion_notify_hdlr(XEvent*);
static void cleanup();

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
};
struct SelectonCircle sel_circ = {0};

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

struct SelectonCircleDims get_curr_sel_dims() {
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

void init_sel_circ_instruments(int x, int y) {
    static struct Item instruments[] = {
        [0] = {NULL, 0},
        [1] = {NULL, 0},
        [2] = {NULL, 0},
        [3] = {NULL, 0},
        [4] = {NULL, 0}
    };

    sel_circ.is_active = True;
    sel_circ.x = x;
    sel_circ.y = y;
    sel_circ.item_count = LENGTH(instruments);
    sel_circ.items = instruments;
}

void draw_selection_circle(int const pointer_x, int const pointer_y) {
    if (!sel_circ.is_active) {
        return;
    }

    struct SelectonCircleDims sel_rect = get_curr_sel_dims();

    clear_selection_circle();

    XClearArea(
        display,
        drawable,
        sel_rect.outer.x - 1,
        sel_rect.outer.y - 1,
        sel_rect.outer.r + 2,
        sel_rect.outer.r + 2,
        True  // Expose to draw background
    );

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
        if (pointer_x != -1 && pointer_y != -1) {
            int const pointer_x_rel = pointer_x - sel_circ.x;
            int const pointer_y_rel = pointer_y - sel_circ.y;
            if (sqrt(
                    pointer_x_rel * pointer_x_rel
                    + pointer_y_rel * pointer_y_rel
                )
                <= sel_rect.outer.r) {
                // FIXME do it right.
                double angle =
                    -atan(pointer_y_rel * 1.0 / pointer_x_rel) / PI * 180;
                if (pointer_x_rel < 0) {
                    angle += 180;
                } else if (pointer_y_rel >= 0) {
                    angle += 360;
                }

                double const segment_deg = segment_rad / PI * 180;
                int const current_segment = angle / segment_deg;

                XFillArc(
                    display,
                    drawable,
                    gc,
                    sel_rect.outer.x,
                    sel_rect.outer.y,
                    sel_rect.outer.r * 2,
                    sel_rect.outer.r * 2,
                    (current_segment * segment_deg) * 64,
                    segment_deg * 64
                );
            }
        }
    }
}

void clear_selection_circle() {
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

void free_sel_circ() {
    sel_circ.is_active = False;
}

void run() {
    XEvent event;

    XSync(display, False);
    while (!done && !XNextEvent(display, &event)) {
        if (handler[event.type]) {
            handler[event.type](&event);
        }
    }
}

void setup() {
    int screen = DefaultScreen(display);

    /* drawing contexts for an window */
    unsigned int myforeground = BlackPixel(display, screen);
    unsigned int mybackground = WhitePixel(display, screen);
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
            | PointerMotionMask
    );

    /* show up window */
    XMapRaised(display, window);
}

void button_press_hdlr(XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    if (e->button == Button3) {
        init_sel_circ_instruments(e->x, e->y);
        draw_selection_circle(-1, -1);
    }
}

void button_release_hdlr(XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    if (e->button == Button3) {
        free_sel_circ();
        clear_selection_circle();
    }
}

void destroy_notify_hdlr(XEvent* event) {}

void expose_hdlr(XEvent* event) {}

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

    draw_selection_circle(e->x, e->y);
}

void cleanup() {
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
}
