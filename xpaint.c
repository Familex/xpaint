#define _POSIX_C_SOURCE 200809L

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// temp solution
#include <pthread.h>
#include <time.h>
#include <unistd.h>

char title[] = "xpaint";
Bool done = False;
unsigned long ANIMATE_SLEEP_MS = 16;
unsigned long MIN_SEL_RECT_DIMENTION_PX = 50;
unsigned long MAX_SEL_RECT_DIMENTION_PX = 200;
unsigned long SEL_CIRC_ANIMATION_TIME_MS = 1000;

struct SelectCircle {
    Bool is_active;
    struct timespec start_time;
    int x;
    int y;
} sel_circ = {0};
pthread_mutex_t sel_circ_mtx;

struct AnimateArgs {
    Display* display;
    Drawable drawable;
    GC gc;
};

void* animate(void*);
XRectangle get_curr_sel_rect();
struct timespec get_time();

int main(int argc, char** argv) {
    pthread_t animate_thread_id;
    KeySym mykey;
    char text[10];

    /* setup display/screen */
    Display* display = XOpenDisplay("");

    int myscreen = DefaultScreen(display);

    /* drawing contexts for an window */
    unsigned int myforeground = BlackPixel(display, myscreen);
    unsigned int mybackground = WhitePixel(display, myscreen);
    XSizeHints hint = {
        .x = 200,
        .y = 300,
        .width = 350,
        .height = 250,
        .flags = PPosition | PSize,
    };

    /* create window */
    Window window = XCreateSimpleWindow(
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

    /* window manager properties (yes, use of StdProp is obsolete) */
    XSetStandardProperties(
        display,
        window,
        title,
        NULL,
        None,
        argv,
        argc,
        &hint
    );

    /* graphics context */
    GC gc = XCreateGC(display, window, 0, 0);
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

    /* FIXME animation thread start */
    struct AnimateArgs animate_thread_args = {
        .display = display,
        .drawable = window,
        .gc = gc,
    };
    pthread_create(
        &animate_thread_id,
        NULL,
        animate,
        (void*)&animate_thread_args
    );

    /* event loop */
    while (!done) {
        /* fetch event */
        XEvent event = {0};
        XNextEvent(display, &event);

        switch (event.type) {
            case Expose: {
                /* Window was showed. */
                if (event.xexpose.count == 0)
                    XDrawImageString(
                        event.xexpose.display,
                        event.xexpose.window,
                        gc,
                        50,
                        50,
                        title,
                        strlen(title)
                    );
            } break;
            case MappingNotify: {
                /* Modifier key was up/down. */
                XRefreshKeyboardMapping(&event.xmapping);
            } break;
            case ButtonPress: {
                XButtonPressedEvent* e = (XButtonPressedEvent*)&event;
                if (e->button == Button3) {
                    pthread_mutex_lock(&sel_circ_mtx);
                    sel_circ.is_active = True;
                    sel_circ.start_time = get_time();
                    sel_circ.x = e->x;
                    sel_circ.y = e->y;
                    pthread_mutex_unlock(&sel_circ_mtx);
                }
            } break;
            case ButtonRelease: {
                XButtonReleasedEvent* e = (XButtonReleasedEvent*)&event;
                if (e->button == Button3) {
                    sel_circ.is_active = False;
                }
            } break;
            case KeyPress: {
                /* Key input. */
                int i = XLookupString(&event.xkey, text, 10, &mykey, 0);
                if (i == 1 && text[0] == 'q') {
                    done = True;
                }
            } break;
            case DestroyNotify: {
                // FIXME feels useless
                XDestroyWindowEvent* e = (XDestroyWindowEvent*)&event;
                if (e->window == window) {
                    done = True;
                }
            } break;
        }
    }

    /* finalization */
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    pthread_join(animate_thread_id, NULL);

    exit(0);
}

void* animate(void* args_v) {
    struct AnimateArgs* args = (struct AnimateArgs*)args_v;
    static Bool sel_circ_flush_done = False;

    while (!done) {
        if (sel_circ.is_active || !sel_circ_flush_done) {
            XRectangle sel_rect = get_curr_sel_rect();

            XClearArea(
                args->display,
                args->drawable,
                sel_rect.x - 1,
                sel_rect.y - 1,
                sel_rect.width + 2,
                sel_rect.height + 2,
                // Expose to draw background
                True
            );

            if (!sel_circ_flush_done) {
                sel_circ_flush_done = True;
            } else {
                sel_circ_flush_done = False;
                // draw circle
                XDrawArc(
                    args->display,
                    args->drawable,
                    args->gc,
                    sel_rect.x,
                    sel_rect.y,
                    sel_rect.width,
                    sel_rect.height,
                    0,
                    360 * 64
                );
                XFlush(args->display);
            }
        }

        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = ANIMATE_SLEEP_MS,
        };
        nanosleep(&ts, &ts);
    }

    pthread_exit(NULL);
}

XRectangle get_curr_sel_rect() {
    pthread_mutex_lock(&sel_circ_mtx);
    // FIXME
    short dimention = 200;

    XRectangle result = {
        .x = sel_circ.x - dimention / 2,
        .y = sel_circ.y - dimention / 2,
        .height = dimention,
        .width = dimention,
    };

    pthread_mutex_unlock(&sel_circ_mtx);

    return result;
}

struct timespec get_time() {
    struct timespec result;

    clock_gettime(CLOCK_REALTIME, &result);

    return result;
}
