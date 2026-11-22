CC = arm-none-eabi-gcc
PYTHON ?= python3
APP ?= hello
APP_NAME ?= $(APP)
BUILD_DIR ?= build
APP_CFLAGS ?=

APP_ELF := $(BUILD_DIR)/$(APP).elf
APP_MAPP := $(BUILD_DIR)/$(APP).mapp
INCLUDES := -I../../picocalc/src
CFLAGS := -std=c11 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
	-ffreestanding -fno-builtin -fno-common -fno-pic -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Wall -Wextra -Werror $(APP_CFLAGS)
LDFLAGS := -nostdlib -Wl,--build-id=none,--emit-relocs,--no-undefined \
	-Wl,-T,app.ld

.PHONY: all mapp clean
all: mapp

mapp: $(APP_MAPP)

$(APP_ELF): $(APP).c app.ld ../../picocalc/src/mapp.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(INCLUDES) $(CFLAGS) $(LDFLAGS) -o $@ $(APP).c

$(APP_MAPP): $(APP_ELF) mapp_pack.py
	$(PYTHON) mapp_pack.py --name "$(APP_NAME)" $< $@
	@echo "Built $@"

clean:
	rm -rf $(BUILD_DIR)
