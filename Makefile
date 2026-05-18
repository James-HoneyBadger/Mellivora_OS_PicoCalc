#
# Mellivora PicoCalc Build System
#
# Root build entry for the Raspberry Pi Pico and Pico 2 Clockwork PicoCalc targets.
#

PICO_DIR = picocalc
PICO_BUILD_DIR ?= $(PICO_DIR)/build
PICO_BOARD ?= pico
PICO_PLATFORM ?= rp2040
PICO_UF2_FAMILY ?= rp2040
PICO_ELF = $(PICO_BUILD_DIR)/mellivora_picocalc.elf
PICO_UF2 = $(PICO_BUILD_DIR)/mellivora_picocalc.uf2
PICOTOOL = $(PICO_BUILD_DIR)/_deps/picotool/picotool

.PHONY: all all-targets picocalc picocalc-pico2 pico2 picocalc-pico2w pico2w picocalc-sdk picocalc-config picocalc-build picocalc-uf2 picocalc-clean picocalc-pico2-clean picocalc-pico2w-clean baseline-report doc-consistency-check command-registry-report command-registry-check quality-check clean

all: picocalc

# Build every supported firmware variant (used by CI release).
# Use recursive sub-makes because the intermediate targets
# (picocalc-config, picocalc-build, picocalc-uf2) are phony and would
# otherwise be considered "already built" after the first variant.
all-targets:
	@$(MAKE) --no-print-directory picocalc
	@$(MAKE) --no-print-directory pico2
	@$(MAKE) --no-print-directory pico2w
	@echo "=== All firmware targets built ==="

picocalc: picocalc-sdk picocalc-config picocalc-build picocalc-uf2
	@echo "=== PicoCalc UF2 ready ==="
	@echo "  $(PICO_UF2)"

picocalc-pico2: PICO_BOARD = pico2
picocalc-pico2: PICO_PLATFORM = rp2350
picocalc-pico2: PICO_UF2_FAMILY = rp2350-arm-s
picocalc-pico2: PICO_BUILD_DIR = $(PICO_DIR)/build-pico2
picocalc-pico2: PICO_ELF = $(PICO_BUILD_DIR)/mellivora_picocalc.elf
picocalc-pico2: PICO_UF2 = $(PICO_BUILD_DIR)/mellivora_picocalc_pico2.uf2
picocalc-pico2: PICOTOOL = $(PICO_BUILD_DIR)/_deps/picotool/picotool
picocalc-pico2: picocalc-sdk picocalc-config picocalc-build picocalc-uf2
	@echo "=== PicoCalc Pico 2 UF2 ready ==="
	@echo "  $(PICO_UF2)"

pico2: picocalc-pico2

picocalc-pico2w: PICO_BOARD = pico2_w
picocalc-pico2w: PICO_PLATFORM = rp2350
picocalc-pico2w: PICO_UF2_FAMILY = rp2350-arm-s
picocalc-pico2w: PICO_BUILD_DIR = $(PICO_DIR)/build-pico2w
picocalc-pico2w: PICO_ELF = $(PICO_BUILD_DIR)/mellivora_picocalc.elf
picocalc-pico2w: PICO_UF2 = $(PICO_BUILD_DIR)/mellivora_picocalc_pico2w.uf2
picocalc-pico2w: PICOTOOL = $(PICO_BUILD_DIR)/_deps/picotool/picotool
picocalc-pico2w: picocalc-sdk picocalc-config picocalc-build picocalc-uf2
	@echo "=== PicoCalc Pico 2W (WiFi) UF2 ready ==="
	@echo "  $(PICO_UF2)"

pico2w: picocalc-pico2w

picocalc-sdk:
	@if [ -z "$(PICO_SDK_PATH)" ] && [ ! -d "$(PICO_DIR)/pico-sdk/external" ]; then \
		echo "=== Fetching Pico SDK ==="; \
		git clone --depth 1 https://github.com/raspberrypi/pico-sdk "$(PICO_DIR)/pico-sdk"; \
	fi
	@if [ -z "$(PICO_SDK_PATH)" ] && [ ! -f "$(PICO_DIR)/pico-sdk/lib/cyw43-driver/src/cyw43.h" ]; then \
		echo "=== Fetching Pico SDK submodules (cyw43-driver, lwip, tinyusb, ...) ==="; \
		git -C "$(PICO_DIR)/pico-sdk" submodule update --init --recursive --depth 1; \
	fi

picocalc-config:
	@echo "=== Configuring PicoCalc target (board: $(PICO_BOARD), platform: $(PICO_PLATFORM)) ==="
	@EXPECTED_SRC="$(CURDIR)/$(PICO_DIR)"; \
	if [ -f "$(PICO_BUILD_DIR)/CMakeCache.txt" ] && ! grep -Fq "CMAKE_HOME_DIRECTORY:INTERNAL=$$EXPECTED_SRC" "$(PICO_BUILD_DIR)/CMakeCache.txt"; then \
		echo "=== Removing stale PicoCalc build cache ==="; \
		rm -rf "$(PICO_BUILD_DIR)"; \
	fi
	@cmake -S "$(PICO_DIR)" -B "$(PICO_BUILD_DIR)" -DPICO_BOARD=$(PICO_BOARD) -DPICO_PLATFORM=$(PICO_PLATFORM)

picocalc-build:
	@echo "=== Building PicoCalc target ==="
	@cmake --build "$(PICO_BUILD_DIR)" -j

picocalc-uf2:
	@echo "=== Generating PicoCalc UF2 ==="
	@test -f "$(PICO_ELF)" || { echo "Missing ELF: $(PICO_ELF)"; exit 1; }
	@test -x "$(PICOTOOL)" || { echo "Missing picotool: $(PICOTOOL)"; exit 1; }
	@"$(PICOTOOL)" uf2 convert --quiet "$(PICO_ELF)" "$(PICO_UF2)" --family "$(PICO_UF2_FAMILY)"

picocalc-clean:
	@echo "=== Cleaning PicoCalc build directory ==="
	@rm -rf "$(PICO_BUILD_DIR)"

picocalc-pico2-clean:
	@echo "=== Cleaning PicoCalc Pico 2 build directory ==="
	@rm -rf "$(PICO_DIR)/build-pico2"

picocalc-pico2w-clean:
	@echo "=== Cleaning PicoCalc Pico 2W build directory ==="
	@rm -rf "$(PICO_DIR)/build-pico2w"

baseline-report:
	@echo "=== Generating baseline quality report ==="
	@chmod +x tools/quality/baseline_report.sh
	@tools/quality/baseline_report.sh reports/baseline-latest.md

size-report:
	@echo "=== Capturing firmware size baseline ==="
	@chmod +x tools/quality/size_baseline.sh
	@tools/quality/size_baseline.sh reports

doc-consistency-check:
	@echo "=== Checking documentation consistency ==="
	@chmod +x tools/quality/doc_consistency_check.sh
	@tools/quality/doc_consistency_check.sh

command-registry-report:
	@echo "=== Building command registry artifacts ==="
	@python3 tools/commands/build_registry.py

command-registry-check:
	@echo "=== Checking command documentation coverage ==="
	@chmod +x tools/quality/command_registry_check.sh
	@tools/quality/command_registry_check.sh

quality-check: baseline-report doc-consistency-check command-registry-check size-report
	@echo "=== Quality checks passed ==="

clean: picocalc-clean picocalc-pico2-clean picocalc-pico2w-clean
	@find . -maxdepth 1 -name "*.lst" -delete
