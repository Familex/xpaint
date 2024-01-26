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

check:
	$(CLANGTIDY) $(HEADER) $(SRC) -- $(INCS)

dev:
	bear -- make verbose

.PHONY: all clean exec verbose check dev
