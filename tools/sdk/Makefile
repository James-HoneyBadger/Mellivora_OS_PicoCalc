CC = arm-none-eabi-gcc
PYTHON ?= python3
# `make` builds every sample; `make APP=plasma` builds just one.
APPS ?= hello plasma
APP ?= $(firstword $(APPS))
BUILD_DIR ?= build
APP_CFLAGS ?=

INCLUDES := -I../../picocalc/src
CFLAGS := -std=c11 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
	-ffreestanding -fno-builtin -fno-common -fno-pic -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Wall -Wextra -Werror $(APP_CFLAGS)
LDFLAGS := -nostdlib -Wl,--build-id=none,--emit-relocs,--no-undefined \
	-Wl,-T,app.ld

.PHONY: all mapp clean

all: $(addprefix $(BUILD_DIR)/,$(addsuffix .mapp,$(APPS)))

# Single-app shorthand: `make APP=plasma`
mapp: $(BUILD_DIR)/$(APP).mapp

$(BUILD_DIR)/%.elf: %.c app.ld ../../picocalc/src/mapp.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(INCLUDES) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD_DIR)/%.mapp: $(BUILD_DIR)/%.elf mapp_pack.py
	$(PYTHON) mapp_pack.py --name "$*" $< $@
	@echo "Built $@"

clean:
	rm -rf $(BUILD_DIR)
