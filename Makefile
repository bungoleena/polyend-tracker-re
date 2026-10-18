# OpenTracker firmware for the original Polyend Tracker hardware (MK66FX1M0).
# make            -> build/opentracker.hex and build/PolyendTrackerOTv<ver digits>.ptf (24 chars)
# make SELFTEST=1 -> build/selftest/PolyendTracker_OTst<DIAG_N>.ptf (24 chars)
# make host-test  -> build and run host-side unit tests
# make fonts      -> regenerate the committed UI font image (src/core/ui/fonts/font_data.*,
#                    sim/third_party/rom_fonts.inc); needs python3 + Pillow
VERSION   ?= 1.9.6
BUILD     := build
# Image file names must be exactly 24 characters (like the stock
# "PolyendTracker_1.9.2.ptf"): the device's update bootloader rejected a
# 45-character name and accepted a 24-character one (docs/HARDWARE_TESTING.md).
# DIAG_N numbers successive diagnostic selftest images (one digit).
DIAG_N    ?= 2
VERNUM    := $(subst .,,$(VERSION))
# make SELFTEST=1 builds a hardware bring-up image that always runs the selftest.
ifeq ($(SELFTEST),1)
BUILD     := build/selftest
DEFS_EXTRA := -DOT_FORCE_SELFTEST=1
PTF_NAME  := PolyendTracker_OTst$(DIAG_N).ptf
else
PTF_NAME  := PolyendTrackerOTv$(VERNUM).ptf
endif
PTF       := $(BUILD)/$(PTF_NAME)
ifneq ($(shell printf '%s' '$(PTF_NAME)' | wc -c | tr -d ' '),24)
$(error image name '$(PTF_NAME)' must be exactly 24 characters (update bootloader limit, docs/HARDWARE_TESTING.md))
endif
# USB identity (third_party/teensy3/PATCHES.txt). Not PJRC's 16C0:0489 (licensed for
# genuine Teensy boards only) and not Polyend's 16D0:0E9F.
# TODO: 1209:0001 is the pid.codes *test* PID -- register a real pid.codes PID
# before any public release.
USB_ID    := -DOT_USB_VID=0x1209 -DOT_USB_PID=0x0001
CORE      := third_party/teensy3
CROSS     ?= arm-none-eabi-
CC        := $(CROSS)gcc
CXX       := $(CROSS)g++
OBJCOPY   := $(CROSS)objcopy
SIZE      := $(CROSS)size

CPUFLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS      := -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI_SERIAL -DLAYOUT_US_ENGLISH \
             -DF_BUS=90000000 -DSERIAL5_TX_BUFFER_SIZE=512 -DSERIAL5_RX_BUFFER_SIZE=512 \
             -DTEENSYDUINO=159 -DARDUINO=10819 -DOT_VERSION=\"$(VERSION)\" -DOT_TARGET_DEVICE=1 \
             $(USB_ID) $(DEFS_EXTRA)
INCS      := -I$(CORE) -Isrc -Isrc/core -Isrc/hal -Ithird_party/fatfs
COMMON    := $(CPUFLAGS) $(DEFS) $(INCS) -O2 -g -ffunction-sections -fdata-sections -Wall -MMD
CFLAGS    := $(COMMON) -std=gnu11
CXXFLAGS  := $(COMMON) -std=gnu++17 -fno-exceptions -fno-rtti -felide-constructors -Wno-psabi
LDFLAGS   := $(CPUFLAGS) -O2 -Wl,--gc-sections,--defsym=__rtc_localtime=0 -T$(CORE)/mk66fx1m0.ld \
             --specs=nano.specs -u _printf_float
LIBS      := -lm -lstdc++

CORE_C    := $(wildcard $(CORE)/*.c)
CORE_CXX  := $(filter-out $(CORE)/main.cpp,$(wildcard $(CORE)/*.cpp))
APP_C     := $(shell find src -name '*.c') third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
APP_CXX   := $(shell find src -name '*.cpp')
OBJS      := $(patsubst %,$(BUILD)/%.o,$(CORE_C) $(CORE_CXX) $(APP_C) $(APP_CXX))

# The stock updater only lists Firmware/ files named "PolyendTrac*.ptf" (spec 06), so the
# image name keeps that prefix for compatibility (and the 24-character length, above).

all: $(BUILD)/opentracker.hex $(PTF)

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/opentracker.elf: $(OBJS)
	$(CXX) $(LDFLAGS) $^ -L$(CORE) $(LIBS) -o $@ -Wl,-Map=$(BUILD)/opentracker.map
	$(SIZE) $@

$(BUILD)/opentracker.hex: $(BUILD)/opentracker.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# The stock updater loads Intel HEX files named *.ptf from /Firmware on the SD card.
$(PTF): $(BUILD)/opentracker.hex
	python3 tools/check_image.py $<
	cp $< $@

HOSTCXX   ?= g++
host-test:
	$(MAKE) -C tests

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
.PHONY: all clean host-test

# Fonts (tools/fonts/gen_fonts.py): the generated files are committed, so
# ordinary builds never run Python.
fonts:
	python3 -I tools/fonts/gen_fonts.py ui src/core/ui/fonts
	$(MAKE) -C sim fonts
.PHONY: fonts

# Desktop simulator (sim/README.md).
sim:
	$(MAKE) -C sim
.PHONY: sim
