LIBS = -lX11 -lm -lXext
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}
CFLAGSD = -std=c99 -pedantic -Wall -g ${INCS} ${LIBS}

CC ?= cc

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

.PHONY: all clean exec verbose
