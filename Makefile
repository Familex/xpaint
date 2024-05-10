include config.mk

SRC = xpaint.c
HEADER = types.h config.h
RES = ./res

all: xpaint ## build application

help: ## display this help
	@echo 'Usage: make [TARGET]... [ARGS="..."]'
	@echo ''
	@echo 'targets:'
	@sed -ne '/@sed/!s/:.*##//p' $(MAKEFILE_LIST) | column -tl 2

xpaint: $(SRC) $(HEADER) $(RES) ## build release application
	@$(CC) -o $@ $(SRC) $(CCFLAGS) -O3 -DNDEBUG

xpaint-d: $(SRC) $(HEADER) $(RES) ## build debug application
	@$(CC) -o $@ $(SRC) $(CCFLAGS) -g

clean: ## remove generated files
	@rm -f xpaint xpaint-d

install: xpaint ## install application
	@mkdir -p $(PREFIX)/bin
	cp -f xpaint $(PREFIX)/bin
	@chmod 755 $(PREFIX)/bin/xpaint
	@mkdir -p $(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < xpaint.1 > $(MANPREFIX)/man1/xpaint.1
	@chmod 644 $(MANPREFIX)/man1/xpaint.1

uninstall: ## uninstall application
	rm -f $(PREFIX)/bin/xpaint
	rm -f $(MANPREFIX)/man1/xpaint.1

run: xpaint-d ## run application. ARGS may be used
	./xpaint-d -v $(ARGS)

check: ## check code with clang-tidy
	$(CLANGTIDY) $(SRC)

dev: clean ## generate dev files
	bear -- make xpaint-d

.PHONY: all clean install uninstall
.PHONY: run check dev
