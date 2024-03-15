INCS = -I/usr/include/freetype2 -I/usr/include
LIBS = -lX11 -lm -lXext -lXft -lXrender
CFLAGS = -std=c99 -pedantic -Wall -O2 ${INCS} ${LIBS}
CFLAGSD = -std=c99 -pedantic -Wall -g -Og ${INCS} ${LIBS}

CC ?= cc
CLANGTIDY ?= clang-tidy

SRC = xpaint.c
HEADER = types.h config.h

all: xpaint ## build application

help: ## display this help
	@echo 'Usage: make [TARGET]... [ARGS="..."]'
	@echo ''
	@echo 'targets:'
	@sed -ne '/@sed/!s/:.*##//p' $(MAKEFILE_LIST) | column -tl 2

xpaint: $(SRC) $(HEADER) ## build release application
	@$(CC) -o $@ $(SRC) $(CFLAGS)

xpaint-d: $(SRC) $(HEADER) ## build debug application
	@$(CC) -o $@ $(SRC) $(CFLAGSD)

exec: xpaint ## run release application. ARGS may be used
	@./xpaint $(ARGS)

verbose: xpaint-d ## run application in verbose mode. ARGS may be used
	@./xpaint-d -v $(ARGS)

watch: xpaint-d ## run debug application. rebuild if sources changed
	@./xpaint-d -v & echo $$! > xpaintpid.tmp
	@inotifywait --event close_write --event move --quiet Makefile $(SRC) $(HEADER)
	@kill `cat xpaintpid.tmp` && rm xpaintpid.tmp
	@$(MAKE) watch

clean: ## remove generated files
	@rm -f xpaint xpaint-d xpaintpid.tmp

check: ## check code with clang-tidy
	$(CLANGTIDY) $(HEADER) $(SRC) -- $(INCS)

dev: ## generate dev files
	bear -- make verbose

.PHONY: all clean exec verbose watch check dev
