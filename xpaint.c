#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char title[] = "xpaint";

int main(int argc, char** argv) {
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
    GC mygc = XCreateGC(display, window, 0, 0);
    XSetBackground(display, mygc, mybackground);
    XSetForeground(display, mygc, myforeground);

    /* allow receiving mouse events */
    XSelectInput(
        display,
        window,
        ButtonPressMask | KeyPressMask | ExposureMask
    );

    /* show up window */
    XMapRaised(display, window);

    /* event loop */
    Bool done = False;
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
                        mygc,
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
            } break;
            case KeyPress: {
                /* Key input. */
                int i = XLookupString(&event.xkey, text, 10, &mykey, 0);
                if (i == 1 && text[0] == 'q')
                    done = True;
            } break;
            case DestroyNotify: {
                XDestroyWindowEvent* e = (XDestroyWindowEvent*)&event;
                if (e->window == window) {
                    done = True;
                }
            } break;
        }
    }

    /* finalization */
    XFreeGC(display, mygc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    exit(0);
}
