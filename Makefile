#### variables section
# change them if needed

# xpaint version
VERSION = 0.10.1

# installation path
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# tools
CC ?= clang
CLANGTIDY ?= clang-tidy
CLANGFORMAT ?= clang-format
PKG_CONFIG ?= pkg-config

#### targets section

SRC = ./xpaint.c
HEADERS = ./types.h ./config.h
DEPS = $(SRC) $(HEADERS) ./res ./lib

all: help ## default target

help: ## display this help
	@echo 'Usage: make [TARGET]... [ARGS="..."]'
	@echo ''
	@echo 'targets:'
	@sed -ne '/@sed/!s/:.*##//p' $(MAKEFILE_LIST) | column -tl 2

run: xpaint-d ## run application with ARGS
	./xpaint-d -v $(ARGS)

xpaint: $(DEPS) ## build release application
	$(MAKE) check-deps
	@$(CC) -o $@ $(SRC) $(CCFLAGS) $(RELEASE_FLAGS)

xpaint-d: $(DEPS) ## build debug application
	$(MAKE) check-deps
	@$(CC) -o $@ $(SRC) $(CCFLAGS) $(DEBUG_FLAGS)

xpaint-d-ns: $(DEPS) ## build debug (no symbols) application
	$(MAKE) check-deps
	@$(CC) -o $@ $(SRC) $(CCFLAGS) $(DEBUG_NO_SYMBOLS_FLAGS)

clean: ## remove generated files
	@$(RM) ./xpaint ./xpaint-d ./xpaint-d-ns ./xpaint-d-no-asan

install: xpaint ## install application
	install -Dm 755 ./xpaint $(PREFIX)/bin/xpaint
	@mkdir -p $(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < ./xpaint.1 > $(MANPREFIX)/man1/xpaint.1
	@chmod 644 $(MANPREFIX)/man1/xpaint.1
	@mkdir -p $(PREFIX)/share
	cp -r --update=all ./share/* $(PREFIX)/share

uninstall: ## uninstall application
	$(RM) $(PREFIX)/bin/xpaint
	$(RM) $(MANPREFIX)/man1/xpaint.1
	$(RM) $(PREFIX)/share/applications/xpaint.desktop
	$(RM) $(PREFIX)/share/icons/hicolor/scalable/apps/xpaint.svg

dist: clean ## create distribution archive
	@mkdir -p xpaint-$(VERSION)
	@cp -r $(DEPS) ./share/ ./Makefile ./README.md ./LICENSE ./xpaint.1 ./xpaint-$(VERSION)
	tar -czf ./xpaint-$(VERSION).tar.gz ./xpaint-$(VERSION)
	@rm -rf ./xpaint-$(VERSION)

check-deps: ## verify all dependencies are available
	@$(PKG_CONFIG) --print-errors --exists x11 xext xft xrender fontconfig

check: ## check code with clang-tidy
	$(CLANGTIDY) --use-color $(SRC)

format: ## format code with clang-format
	$(CLANGFORMAT) -i $(SRC) $(HEADERS)

dev: clean ## generate dev files
	bear -- $(MAKE) ./xpaint-d

valgrind: ## debug with valgrind
	# sanitizer breaks valgrind
	$(MAKE) DEBUG_FLAGS="$(DEBUG_FLAGS) -fno-sanitize=address" clean xpaint-d
	mv ./xpaint-d ./xpaint-d-no-asan
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--suppressions=./.valgrind.supp $(ARGS) ./xpaint-d-no-asan

.PHONY: all help run clean install uninstall dist check-deps check format dev valgrind

#### compiler and linker flags

LANG_FLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -Wshadow
RELEASE_FLAGS = -flto=auto -O2 -DNDEBUG -D_FORTIFY_SOURCE=2
DEBUG_FLAGS = -g -fsanitize=address -static-libasan
DEBUG_NO_SYMBOLS_FLAGS = -O0 -DNDEBUG -Wno-error

INCS = $(shell $(PKG_CONFIG) --cflags x11 xext xft xrender fontconfig)
LIBS = $(shell $(PKG_CONFIG) --libs x11 xext xft xrender fontconfig) -lm
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
