# Host simulator: build a tools/sdk app as a native desktop binary against
# the same ABI and the same core/gfx.c + core/mixer.c the firmware links.
#
#   make APP=plasma            build and run (screenshot -> shots/plasma.png)
#   make APP=hello             text app; output on stdout
#   make APP=plasma run=0      just build
#   MAPP_AUTOQUIT_MS=2000 ...  override the virtual-key timeout
#
# Headless by design: no SDL, no window. The virtual key lands after
# MAPP_AUTOQUIT_MS so the app's loop can run its course and exit.

CC ?= cc
APP ?= plasma
run ?= 1
CFLAGS ?= -O2 -g -Wall -Wextra -Wshadow -Wundef -Werror
CPPFLAGS += -I../../picocalc/src -I.
LDLIBS := -lz

CORE := ../../picocalc/src/core/gfx.c \
        ../../picocalc/src/core/mixer.c

BIN := build/$(APP)

.PHONY: all clean

all: $(BIN)
	@if [ "$(run)" = "1" ]; then ./$(BIN); fi

$(BIN): runtime.c ../sdk/$(APP).c $(CORE) ../../picocalc/src/mapp.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ runtime.c ../sdk/$(APP).c $(CORE) $(LDLIBS)

clean:
	rm -rf build shots
