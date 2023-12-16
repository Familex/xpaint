LIBS = -lX11 -lGL -lGLU -lm
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}

CC = cc

SRC = xpaint.c

all: xpaint

clean:
	@rm -f xpaint ${OBJ}

xpaint: ${SRC} config.h
	@${CC} -o $@ ${SRC} ${CFLAGS}

exec: xpaint
	@./xpaint

.PHONY: all clean exec
