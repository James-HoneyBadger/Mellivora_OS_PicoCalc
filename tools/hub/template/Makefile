# Build this app for the PicoCalc and for the desktop.
#
#   make            -> build/NAME.mapp   (copy to /apps on the SD card)
#   make sim        -> run here, screenshot in shots/
#   make release    -> build/NAME.mapp plus its .sha256 sidecar
#
# MELLIVORA points at a Mellivora checkout:
#   make MELLIVORA=/path/to/Mellivora_OS_PicoCalc

MELLIVORA ?= ../Mellivora_OS_PicoCalc
SDK := $(MELLIVORA)/tools/sdk
SIM := $(MELLIVORA)/tools/sim
BUILD := build

_pkg_name  := $(shell sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' mellivora.json)
_pkg_title := $(shell sed -n 's/.*"title"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' mellivora.json)
_pkg_entry := $(shell sed -n 's/.*"entry"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' mellivora.json)

NAME ?= $(_pkg_name)
TITLE ?= $(_pkg_title)
ENTRY ?= $(_pkg_entry)

.PHONY: all sim release clean check

all: check $(BUILD)/$(NAME).mapp

# Delegate to the upstream SDK so the flags, linker script, and packer live
# in exactly one place.
$(BUILD)/$(NAME).mapp: $(ENTRY) $(SDK)/app.ld
	@mkdir -p $(BUILD)
	$(MAKE) -C $(SDK) APP=$(NAME) APP_NAME="$(TITLE)" \
		APP_SRC=$(abspath $(ENTRY)) BUILD_DIR=$(abspath $(BUILD))

sim: check
	MAPP_SHOTS_DIR=$(abspath shots) \
		$(MAKE) -C $(SIM) APP=$(NAME) APP_SRC=$(abspath $(ENTRY)) run=1

# A release artifact plus the digest the Hub indexer prefers.
release: all
	@if command -v sha256sum >/dev/null 2>&1; then \
		sha256sum $(BUILD)/$(NAME).mapp | cut -d' ' -f1 > $(BUILD)/$(NAME).mapp.sha256; \
	else \
		shasum -a 256 $(BUILD)/$(NAME).mapp | cut -d' ' -f1 > $(BUILD)/$(NAME).mapp.sha256; \
	fi
	@echo "release: $(BUILD)/$(NAME).mapp"
	@cat $(BUILD)/$(NAME).mapp.sha256

check:
	@test -f $(SDK)/Makefile || { \
		echo "MELLIVORA does not point at a Mellivora checkout ($(MELLIVORA))"; \
		echo "clone it, then re-run: make MELLIVORA=/path/to/Mellivora_OS_PicoCalc"; \
		exit 1; }

clean:
	rm -rf $(BUILD) shots
