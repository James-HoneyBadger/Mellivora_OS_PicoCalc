#
# Mellivora PicoCalc Build System
#
# Root build entry for the RP2040 Clockwork PicoCalc target.
#

PICO_DIR = picocalc
PICO_BUILD_DIR = $(PICO_DIR)/build
PICO_BOARD ?= pico

.PHONY: all picocalc picocalc-config picocalc-build picocalc-clean clean

all: picocalc

picocalc: picocalc-config picocalc-build
	@echo "=== PicoCalc UF2 ready ==="
	@echo "  $(PICO_BUILD_DIR)/mellivora_picocalc.uf2"

picocalc-config:
	@echo "=== Configuring PicoCalc target (board: $(PICO_BOARD)) ==="
	@cmake -S "$(PICO_DIR)" -B "$(PICO_BUILD_DIR)" -DPICO_BOARD=$(PICO_BOARD)

picocalc-build:
	@echo "=== Building PicoCalc target ==="
	@cmake --build "$(PICO_BUILD_DIR)" -j

picocalc-clean:
	@echo "=== Cleaning PicoCalc build directory ==="
	@rm -rf "$(PICO_BUILD_DIR)"

clean: picocalc-clean
	@find . -maxdepth 1 -name "*.lst" -delete
