#### variables section
# change them if needed

# xpaint version
VERSION = 0.8.0

# installation path
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# tools
CC ?= clang
CLANGTIDY ?= clang-tidy

#### targets section

SRC = ./xpaint.c
DEPS = $(SRC) ./res ./types.h ./config.h

all: help ## default target

help: ## display this help
	@echo 'Usage: make [TARGET]... [ARGS="..."]'
	@echo ''
	@echo 'targets:'
	@sed -ne '/@sed/!s/:.*##//p' $(MAKEFILE_LIST) | column -tl 2

run: xpaint-d ## run application with ARGS
	./xpaint-d -v $(ARGS)

xpaint: $(DEPS) ## build release application
	@$(CC) -o $@ $(SRC) $(CCFLAGS) $(RELEASE_FLAGS)

xpaint-d: $(DEPS) ## build debug application
	@$(CC) -o $@ $(SRC) $(CCFLAGS) $(DEBUG_FLAGS)

xpaint-d-ns: $(DEPS) ## build debug (no symbols) application
	@$(CC) -o $@ $(SRC) $(CCFLAGS) $(DEBUG_NO_SYMBOLS_FLAGS)

clean: ## remove generated files
	@rm -f ./xpaint ./xpaint-d ./xpaint-d-ns

install: xpaint ## install application
	@mkdir -p $(PREFIX)/bin
	cp -f ./xpaint $(PREFIX)/bin
	@chmod 755 $(PREFIX)/bin/xpaint
	@mkdir -p $(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < ./xpaint.1 > $(MANPREFIX)/man1/xpaint.1
	@chmod 644 $(MANPREFIX)/man1/xpaint.1
	@mkdir -p $(PREFIX)/share
	cp -r --update=all ./share/* $(PREFIX)/share

uninstall: ## uninstall application
	rm -f $(PREFIX)/bin/xpaint
	rm -f $(MANPREFIX)/man1/xpaint.1
	rm -f $(PREFIX)/share/applications/xpaint.desktop
	rm -f $(PREFIX)/share/icons/hicolor/scalable/apps/xpaint.svg

check: ## check code with clang-tidy
	$(CLANGTIDY) --use-color $(SRC)

dev: clean ## generate dev files
	bear -- $(MAKE) ./xpaint-d

valgrind: xpaint-d ## debug with valgrind
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--suppressions=./.valgrind.supp $(ARGS) ./xpaint-d

.PHONY: all help run clean install uninstall check dev valgrind

#### compiler and linker flags

LANG_FLAGS = -std=c99 -pedantic -Wall -Wextra -Werror
RELEASE_FLAGS = -flto -O2 -DNDEBUG
DEBUG_FLAGS = -g -fsanitize=address -static-libasan
DEBUG_NO_SYMBOLS_FLAGS = -O0 -DNDEBUG -Wno-error

INCS = $(shell pkg-config --cflags x11 xext xft xrender fontconfig)
LIBS = $(shell pkg-config --libs x11 xext xft xrender fontconfig) -lm
DEFINES = -DVERSION=\"$(VERSION)\" \
	$(shell \
		for res in ./res/* ; do \
			echo -n $$(basename $$res) \
				| tr '-' '_' \
				| sed -En 's/(.*)\..*/\U-DRES_SZ_\1/p'; \
			echo -n "=$$(stat -c %s $$res) "; \
		done \
	)
CCFLAGS = $(LANG_FLAGS) $(INCS) $(LIBS) $(DEFINES)
