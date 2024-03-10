INCS = -I/usr/include/freetype2 -I/usr/include
LIBS = -lX11 -lm -lXext -lXft -lXrender
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}
CFLAGSD = -std=c99 -pedantic -Wall -g -Og ${INCS} ${LIBS}

CC ?= cc
CLANGTIDY ?= clang-tidy

SRC = xpaint.c
HEADER = types.h config.h

all: xpaint

clean:
	@rm -f xpaint

xpaint: $(SRC) $(HEADER)
	@$(CC) -o $@ $(SRC) $(CFLAGS)

xpaint-d: $(SRC) $(HEADER)
	@$(CC) -o $@ $(SRC) $(CFLAGSD)

exec: xpaint
	@./xpaint

verbose: xpaint-d
	@./xpaint-d -v

watch: xpaint-d
	@./xpaint-d -v & echo $$! > xpaintpid.tmp
	@inotifywait --event close_write --event move --quiet Makefile $(SRC) $(HEADER)
	@kill `cat xpaintpid.tmp` && rm xpaintpid.tmp
	@$(MAKE) watch

check:
	$(CLANGTIDY) $(HEADER) $(SRC) -- $(INCS)

dev:
	bear -- make verbose

.PHONY: all clean exec verbose watch check dev
