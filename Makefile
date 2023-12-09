INCS = 
LIBS = -lX11 -lGL -lGLU
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}

CC = cc

all: clean xpaint

clean:
	@rm -rf build

xpaint:
	@mkdir build
	@${CC} -o build/xpaint xpaint.c ${CFLAGS}

.PHONY: all clean xpaint
