#define _POSIX_C_SOURCE 200809L

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define LENGTH(X) (sizeof X / sizeof X[0])

#include "config.h"

struct Item {
    void (*on_select)();
    int hicon;
};

struct SelectCircle {
    Bool is_active;
    int x;
    int y;
    unsigned int item_count;
    struct Item* items;
} sel_circ = {0};
pthread_mutex_t sel_circ_mtx;

static void die(char const* errstr, ...);

static void init_sel_circ_instruments(int, int);
static void free_sel_circ();

static XRectangle get_curr_sel_rect();

static void draw_selection_circle();
static void clear_selection_circle();

static void setup();
static void run();
static void button_press_hdlr(XEvent*);
static void button_release_hdlr(XEvent*);
static void destroy_notify_hdlr(XEvent*);
static void expose_hdlr(XEvent*);
static void key_press_hdlr(XEvent*);
static void mapping_notify_hdlr(XEvent*);
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
};

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

XRectangle get_curr_sel_rect() {
    XRectangle result = {
        .x = sel_circ.x - SELECTION_RECT_DIMENTION_PX / 2,
        .y = sel_circ.y - SELECTION_RECT_DIMENTION_PX / 2,
        .height = SELECTION_RECT_DIMENTION_PX,
        .width = SELECTION_RECT_DIMENTION_PX,
    };

    return result;
}

void init_sel_circ_instruments(int x, int y) {
    static struct Item instruments[] = {[0] = {NULL, 0}};

    sel_circ.is_active = True;
    sel_circ.x = x;
    sel_circ.y = y;
    sel_circ.item_count = LENGTH(instruments);
    sel_circ.items = instruments;
}

void draw_selection_circle() {
    assert(sel_circ.is_active);

    XRectangle sel_rect = get_curr_sel_rect();

    XClearArea(
        display,
        drawable,
        sel_rect.x - 1,
        sel_rect.y - 1,
        sel_rect.width + 2,
        sel_rect.height + 2,
        True  // Expose to draw background
    );

    XDrawArc(
        display,
        drawable,
        gc,
        sel_rect.x,
        sel_rect.y,
        sel_rect.width,
        sel_rect.height,
        0,
        360 * 64
    );
}

void clear_selection_circle() {
    XRectangle sel_rect = get_curr_sel_rect();

    XClearArea(
        display,
        drawable,
        sel_rect.x - 1,
        sel_rect.y - 1,
        sel_rect.width + 2,
        sel_rect.height + 2,
        True  // Expose to draw background
    );
}

void free_sel_circ() {
    sel_circ.is_active = False;
}

static void run() {
    XEvent event;

    XSync(display, False);
    while (!done && !XNextEvent(display, &event)) {
        if (handler[event.type]) {
            handler[event.type](&event);
        }
    }
}

static void setup() {
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

    XSetStandardProperties(display, window, title, NULL, None, 0, NULL, &hint);

    /* graphics context */
    gc = XCreateGC(display, window, 0, 0);
    XSetBackground(display, gc, mybackground);
    XSetForeground(display, gc, myforeground);

    /* allow receiving mouse events */
    XSelectInput(
        display,
        window,
        ButtonPressMask | ButtonReleaseMask | KeyPressMask | ExposureMask
    );

    /* show up window */
    XMapRaised(display, window);
}

static void button_press_hdlr(XEvent* event) {
    XButtonPressedEvent* e = (XButtonPressedEvent*)event;
    if (e->button == Button3) {
        init_sel_circ_instruments(e->x, e->y);
        draw_selection_circle();
    }
}

static void button_release_hdlr(XEvent* event) {
    XButtonReleasedEvent* e = (XButtonReleasedEvent*)event;
    if (e->button == Button3) {
        free_sel_circ();
        clear_selection_circle();
    }
}

static void destroy_notify_hdlr(XEvent* event) {}

static void expose_hdlr(XEvent* event) {}

static void key_press_hdlr(XEvent* event) {
    static char text[10];
    KeySym key;

    int i = XLookupString(&event->xkey, text, 10, &key, 0);
    if (i == 1 && text[0] == 'q') {
        done = True;
    }
}

static void mapping_notify_hdlr(XEvent* event) {
    XRefreshKeyboardMapping(&event->xmapping);
}

static void cleanup() {
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
}
