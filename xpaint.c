#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// temp solution
#include <pthread.h>
#include <unistd.h>

char title[] = "xpaint";
Bool done = False;

struct SelectCircle {
    Bool is_active;
    int start_time_ms;
    int x;
    int y;
} sel_circ = {0};

struct AnimateArgs {
    Display* display;
    Drawable drawable;
    GC gc;
};

void* animate(void*);

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
                    sel_circ.is_active = True;
                    sel_circ.start_time_ms = e->time;
                    sel_circ.x = e->x;
                    sel_circ.y = e->y;
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

    while (!done) {
        XDrawRectangle(args->display, args->drawable, args->gc, 30, 30, 40, 30);

        sleep(2);
    }

    pthread_exit(NULL);
}
