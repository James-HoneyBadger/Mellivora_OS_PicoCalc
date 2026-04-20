#
# Mellivora PicoCalc Build System
#
# Root build entry for the RP2040 Clockwork PicoCalc target.
#

PICO_DIR = picocalc
PICO_BUILD_DIR = $(PICO_DIR)/build
PICO_BOARD ?= pico
PICO_ELF = $(PICO_BUILD_DIR)/mellivora_picocalc.elf
PICO_UF2 = $(PICO_BUILD_DIR)/mellivora_picocalc.uf2
PICOTOOL = $(PICO_BUILD_DIR)/_deps/picotool/picotool

.PHONY: all picocalc picocalc-sdk picocalc-config picocalc-build picocalc-uf2 picocalc-clean clean

all: picocalc

picocalc: picocalc-sdk picocalc-config picocalc-build picocalc-uf2
	@echo "=== PicoCalc UF2 ready ==="
	@echo "  $(PICO_UF2)"

picocalc-sdk:
	@if [ -z "$(PICO_SDK_PATH)" ] && [ ! -d "$(PICO_DIR)/pico-sdk/external" ]; then \
		echo "=== Fetching Pico SDK ==="; \
		git clone --depth 1 https://github.com/raspberrypi/pico-sdk "$(PICO_DIR)/pico-sdk"; \
	fi

picocalc-config:
	@echo "=== Configuring PicoCalc target (board: $(PICO_BOARD)) ==="
	@EXPECTED_SRC="$(CURDIR)/$(PICO_DIR)"; \
	if [ -f "$(PICO_BUILD_DIR)/CMakeCache.txt" ] && ! grep -Fq "CMAKE_HOME_DIRECTORY:INTERNAL=$$EXPECTED_SRC" "$(PICO_BUILD_DIR)/CMakeCache.txt"; then \
		echo "=== Removing stale PicoCalc build cache ==="; \
		rm -rf "$(PICO_BUILD_DIR)"; \
	fi
	@cmake -S "$(PICO_DIR)" -B "$(PICO_BUILD_DIR)" -DPICO_BOARD=$(PICO_BOARD)

picocalc-build:
	@echo "=== Building PicoCalc target ==="
	@cmake --build "$(PICO_BUILD_DIR)" -j

picocalc-uf2:
	@echo "=== Generating PicoCalc UF2 ==="
	@test -f "$(PICO_ELF)" || { echo "Missing ELF: $(PICO_ELF)"; exit 1; }
	@test -x "$(PICOTOOL)" || { echo "Missing picotool: $(PICOTOOL)"; exit 1; }
	@"$(PICOTOOL)" uf2 convert --quiet "$(PICO_ELF)" "$(PICO_UF2)" --family rp2040

picocalc-clean:
	@echo "=== Cleaning PicoCalc build directory ==="
	@rm -rf "$(PICO_BUILD_DIR)"

clean: picocalc-clean
	@find . -maxdepth 1 -name "*.lst" -delete
