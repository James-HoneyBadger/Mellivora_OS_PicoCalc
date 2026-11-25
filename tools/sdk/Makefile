CC = arm-none-eabi-gcc
PYTHON ?= python3
# `make` builds every sample; `make APP=plasma` builds just one.
APPS ?= hello plasma lifelab
APP ?= $(firstword $(APPS))
# Set APP_SRC to build a .c file from outside this directory (the Hub app
# template does this), in which case only $(APP) is built.
APP_SRC ?=
APP_NAME ?= $(APP)
BUILD_DIR ?= build
APP_CFLAGS ?=

INCLUDES := -I../../picocalc/src
CFLAGS := -std=c11 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
	-ffreestanding -fno-builtin -fno-common -fno-pic -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Wall -Wextra -Werror $(APP_CFLAGS)
LDFLAGS := -nostdlib -Wl,--build-id=none,--emit-relocs,--no-undefined \
	-Wl,-T,app.ld

.PHONY: all mapp clean

ifeq ($(APP_SRC),)
all: $(addprefix $(BUILD_DIR)/,$(addsuffix .mapp,$(APPS)))
else
all: $(BUILD_DIR)/$(APP).mapp
endif

# Single-app shorthand: `make APP=plasma`
mapp: $(BUILD_DIR)/$(APP).mapp

# In-tree samples compile by pattern rule; an out-of-tree source gets an
# explicit rule so `APP_SRC` works from another checkout.
ifeq ($(APP_SRC),)
$(BUILD_DIR)/%.elf: %.c app.ld ../../picocalc/src/mapp.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(INCLUDES) $(CFLAGS) $(LDFLAGS) -o $@ $<
else
$(BUILD_DIR)/$(APP).elf: $(APP_SRC) app.ld ../../picocalc/src/mapp.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(INCLUDES) $(CFLAGS) $(LDFLAGS) -o $@ $(APP_SRC)
endif

$(BUILD_DIR)/%.mapp: $(BUILD_DIR)/%.elf mapp_pack.py
	$(PYTHON) mapp_pack.py --name "$(if $(APP_NAME),$(APP_NAME),$*)" $< $@
	@echo "Built $@"

clean:
	rm -rf $(BUILD_DIR)
