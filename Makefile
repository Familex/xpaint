LIBS = -lX11 -lm
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}
CFLAGSD = -std=c99 -pedantic -Wall -g ${INCS} ${LIBS}

CC ?= cc

SRC = xpaint.c

all: xpaint

clean:
	@rm -f xpaint

xpaint: ${SRC} config.h
	@${CC} -o $@ ${SRC} ${CFLAGS}

xpaint-d: ${SRC} config.h
	@${CC} -o $@ ${SRC} ${CFLAGSD}

exec: xpaint
	@./xpaint

verbose: xpaint-d
	@./xpaint-d -v

.PHONY: all clean exec verbose
