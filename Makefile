LIBS = -lX11 -lm
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}
CFLAGSD = -std=c99 -pedantic -Wall -g ${INCS} ${LIBS}

CC ?= cc

# use headers as make-target dependencies, not compile symbols
SRC = xpaint.c types.h config.h

all: xpaint

clean:
	@rm -f xpaint

xpaint: ${SRC}
	@${CC} -o $@ ${SRC} ${CFLAGS}

xpaint-d: ${SRC}
	@${CC} -o $@ ${SRC} ${CFLAGSD}

exec: xpaint
	@./xpaint

verbose: xpaint-d
	@./xpaint-d -v

.PHONY: all clean exec verbose
