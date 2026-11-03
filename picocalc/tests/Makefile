#
# Host tests for pure logic and the FAT driver with an in-memory SD card.
#
# These compile with the system C compiler, not the ARM toolchain, so they
# run quickly in CI and locally without hardware or the Pico SDK.
#

CC ?= cc
SANITIZE ?= 1

CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wshadow -Wundef -Werror
CPPFLAGS += -I../src -I.

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SRC_DIR := ../src
SOURCES := \
	$(SRC_DIR)/apps/app_shared.c \
	$(SRC_DIR)/core/expr.c \
	$(SRC_DIR)/core/shell_parse.c \
	$(SRC_DIR)/core/path_util.c \
	$(SRC_DIR)/core/fatname.c \
	$(SRC_DIR)/core/simple_re.c \
	$(SRC_DIR)/core/glob.c \
	$(SRC_DIR)/core/textutil.c \
	$(SRC_DIR)/core/history.c \
	$(SRC_DIR)/core/datetime.c \
	$(SRC_DIR)/fat.c \
	$(SRC_DIR)/core/lfn.c

TEST_SOURCES := $(wildcard test_*.c)
BIN := build/run_tests

.PHONY: all run clean

all: run

$(BIN): $(SOURCES) $(TEST_SOURCES) test_util.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(TEST_SOURCES) -o $@ $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build
