# change this file before installation

# xpaint version
VERSION = 0.2.0

# instalation path
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

CC ?= cc
CLANGTIDY ?= clang-tidy

# compiler and linker flags
INCS = -I/usr/X11R6/include -I/usr/include/freetype2
LIBS = -L/usr/X11R6/lib -lX11 -lX11 -lm -lXext -lXft -lXrender
DEFINES = -DVERSION=\"$(VERSION)\" \
	$(foreach res, \
		$(wildcard $(RES)/*), \
		$(shell \
			echo $(basename $(res)) \
			| tr '-' '_' \
			| sed -En 's/\.\/res\/(.*)/\U-DRES_SZ_\1/p' \
		)=$(shell stat -c %s $(res)) \
	)
CCFLAGS = -std=c99 -pedantic -Wall $(INCS) $(LIBS) $(DEFINES)
