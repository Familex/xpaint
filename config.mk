# change this file before installation

# xpaint version
VERSION = 0.1.0

# instalation path
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# compiler and linker flags
INCS = -I/usr/X11R6/include -I/usr/include/freetype2
LIBS = -L/usr/X11R6/lib -lX11 -lX11 -lm -lXext -lXft -lXrender
DEFINES = -DVERSION=\"$(VERSION)\"
CCFLAGS = -std=c99 -pedantic -Wall $(INCS) $(LIBS) $(DEFINES)
