# ---------------------------------------------------------------------------
# Build helpers for the Matter soil sensor.
#
#   make pio                  build with PlatformIO
#   make idf                  build with native ESP-IDF
#   make full                 build (BACKEND, default pio) + full image + OTA image
#   make package              full image + OTA image from the existing build
#
# Vendor/product ID and version are read from the build's generated
# config/sdkconfig.h, so the OTA header always matches the firmware it wraps.
# ---------------------------------------------------------------------------

SHELL       := bash
.SHELLFLAGS := -eu -o pipefail -c
.ONESHELL:
.DEFAULT_GOAL := help

PROJECT    := matter_soil_sensor
BACKEND    ?= pio
IDF_EXPORT ?= $(HOME)/esp/esp-idf-v5.4.1/export.sh
ESPTOOL    ?= esptool
PYTHON     ?= python3
DIST       ?= dist

OTA_TOOL := managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py

ifeq ($(BACKEND),pio)
  BUILD_DIR  := .pio/build/xiao_soil_sensor
  SDKCONFIG  := sdkconfig.xiao_soil_sensor
  BOOTLOADER := $(BUILD_DIR)/bootloader.bin
  PARTITIONS := $(BUILD_DIR)/partitions.bin
  APP        := $(BUILD_DIR)/firmware.bin
else ifeq ($(BACKEND),idf)
  BUILD_DIR  := build
  SDKCONFIG  := sdkconfig
  BOOTLOADER := $(BUILD_DIR)/bootloader/bootloader.bin
  PARTITIONS := $(BUILD_DIR)/partition_table/partition-table.bin
  APP        := $(BUILD_DIR)/$(PROJECT).bin
else
  $(error BACKEND must be 'pio' or 'idf', got '$(BACKEND)')
endif
OTADATA     := $(BUILD_DIR)/ota_data_initial.bin
SDKCONFIG_H := $(BUILD_DIR)/config/sdkconfig.h

.PHONY: help pio idf full package clean

help:
	@printf '%s\n' \
	  'Targets:' \
	  '  pio        Build with PlatformIO  (pio run)' \
	  '  idf        Build with ESP-IDF     (idf.py build, sources IDF_EXPORT)' \
	  '  full       Build with BACKEND, then create the full flash image and the OTA image' \
	  '  package    Create the full flash image and the OTA image from the existing build' \
	  '  clean      Remove $(DIST)/' \
	  '' \
	  'Variables (current value):' \
	  '  BACKEND=$(BACKEND)  (pio | idf)' \
	  '  IDF_EXPORT=$(IDF_EXPORT)' \
	  '  ESPTOOL=$(ESPTOOL)  PYTHON=$(PYTHON)  DIST=$(DIST)' \
	  '' \
	  'Examples:' \
	  '  make full' \
	  '  make full BACKEND=idf'

pio:
	pio run

# -DIDF_TARGET selects the chip on a fresh tree and is a no-op on an existing one,
# so `idf.py set-target` (which wipes build/ and sdkconfig) is never needed.
idf:
	@[ -f "$(IDF_EXPORT)" ] || { echo "ESP-IDF export script not found: $(IDF_EXPORT) (set IDF_EXPORT=...)"; exit 1; }
	. "$(IDF_EXPORT)" > /dev/null
	idf.py -DIDF_TARGET=esp32c6 build

full: $(BACKEND)
	@$(MAKE) --no-print-directory package BACKEND=$(BACKEND)

package:
	@for f in "$(SDKCONFIG_H)" "$(BOOTLOADER)" "$(PARTITIONS)" "$(OTADATA)" "$(APP)"; do
	  [ -f "$$f" ] || { echo "Missing $$f -- run 'make $(BACKEND)' first."; exit 1; }
	done

	cfg() { sed -n "s/^#define CONFIG_$$1 //p" "$(SDKCONFIG_H)" | tr -d '"'; }
	vid=$$(cfg DEVICE_VENDOR_ID)
	pid=$$(cfg DEVICE_PRODUCT_ID)
	vn=$$(cfg DEVICE_SOFTWARE_VERSION_NUMBER)
	vs=$$(cfg APP_PROJECT_VER)
	[ -n "$$vid" ] && [ -n "$$pid" ] && [ -n "$$vn" ] && [ -n "$$vs" ] \
	  || { echo "Could not read VID/PID/version from $(SDKCONFIG_H)"; exit 1; }

	# A stale generated sdkconfig silently keeps the old version (see README).
	want=$$(sed -n 's/^CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER=//p' sdkconfig.defaults)
	if [ -n "$$want" ] && [ "$$want" != "$$vn" ]; then
	  echo "WARNING: sdkconfig.defaults says version $$want but the build has $$vn."
	  echo "         Delete $(SDKCONFIG) and rebuild to pick up the new version."
	fi

	mkdir -p "$(DIST)"
	full="$(DIST)/$(PROJECT)_v$${vn}_full.bin"
	ota="$(DIST)/$(PROJECT)_v$${vn}.ota"

	echo "==> $$full"
	$(ESPTOOL) --chip esp32c6 merge-bin -o "$$full" \
	  --flash-mode dio --flash-freq 80m --flash-size 4MB \
	  0x0     "$(BOOTLOADER)" \
	  0x8000  "$(PARTITIONS)" \
	  0xe000  "$(OTADATA)" \
	  0x10000 "$(APP)"

	echo "==> $$ota  (VID $$vid, PID $$pid, version $$vn / \"$$vs\")"
	$(PYTHON) "$(OTA_TOOL)" create -v "$$vid" -p "$$pid" -vn "$$vn" -vs "$$vs" \
	  -da sha256 "$(APP)" "$$ota"

	echo
	echo "Flash a new device:  $(ESPTOOL) --chip esp32c6 -p /dev/ttyACM0 erase-flash"
	echo "                     $(ESPTOOL) --chip esp32c6 -p /dev/ttyACM0 write-flash 0x0 $$full"
	echo "OTA update:          serve $$ota from an OTA provider"

clean:
	rm -rf "$(DIST)"
	rm -f sdkconfig.xiao_soil_sensor
