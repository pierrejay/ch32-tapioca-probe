PROJECT := tapioca-probe
PROFILE ?= jtagswd

CH32FUN := sdk/ch32fun/ch32fun
MINICHLINK_DIR := sdk/ch32fun/minichlink
MINICHLINK := $(MINICHLINK_DIR)/minichlink
LINKER_SCRIPT := ldscript/Link_CH32X035_pioc.ld
BUILD_ROOT := build
BUILD_DIR := $(BUILD_ROOT)/$(PROFILE)
TARGET := $(BUILD_DIR)/$(PROJECT)-$(PROFILE)

ARCH_FLAGS := -march=rv32imacxw -mabi=ilp32
REPO_RISCV_PREFIX := $(abspath sdk/toolchain/bin/riscv-none-embed)
PATH_RISCV_PREFIX := $(shell \
	for prefix in riscv-none-embed riscv-wch-elf riscv-none-elf riscv64-none-elf \
		riscv64-unknown-elf riscv32-unknown-elf; do \
		command -v $$prefix-gcc >/dev/null 2>&1 || continue; \
		$$prefix-gcc $(ARCH_FLAGS) -x c -c /dev/null -o /dev/null >/dev/null 2>&1 || continue; \
		printf '%s' $$prefix; break; \
	done)

# Priority: explicit environment/command-line override, repository-local
# known-good toolchain, then a compatible compiler from PATH.
ifeq ($(origin RISCV_PREFIX),undefined)
ifneq ($(wildcard $(REPO_RISCV_PREFIX)-gcc),)
RISCV_PREFIX := $(REPO_RISCV_PREFIX)
else
RISCV_PREFIX := $(PATH_RISCV_PREFIX)
endif
endif

CC := $(RISCV_PREFIX)-gcc
CXX := $(RISCV_PREFIX)-g++
OBJCOPY := $(RISCV_PREFIX)-objcopy
SIZE := $(RISCV_PREFIX)-size
HOST_CXX ?= c++
PYTHON ?= python3
WCHISP ?= wchisp
EXTRA_CPPFLAGS ?=

CODEGEN_FLAGS := $(ARCH_FLAGS) -Os -g -flto -ffunction-sections -fdata-sections \
	-fmessage-length=0 -msmall-data-limit=8 -msave-restore -fsigned-char \
	-fno-common
COMMON_FLAGS := $(CODEGEN_FLAGS) -Wall -Wextra -MMD -MP \
	-DCH32X035F8 -DCH32X03X -DCH32X03x -DCH32X035 \
	-DFUNCONF_ENABLE_HPE=1 \
	-Isrc -Isrc/hal -I$(CH32FUN) $(EXTRA_CPPFLAGS)
CFLAGS := $(COMMON_FLAGS) -std=gnu11
CXXFLAGS := $(COMMON_FLAGS) -std=gnu++17 -fno-exceptions -fno-rtti \
	-fno-threadsafe-statics -fno-use-cxa-atexit
LDFLAGS := $(CODEGEN_FLAGS) -static-libgcc -nostdlib \
	-T$(LINKER_SCRIPT) -Wl,--gc-sections -Wl,--print-memory-usage \
	-Wl,-Map=$(TARGET).map
LDLIBS := -lgcc

COMMON_SOURCES := \
	src/hal/interrupts.cpp \
	src/hal/time.cpp

JTAGSWD_SOURCES := \
	src/main_jtagswd.cpp \
	src/dirtyjtag/ch32_jtag.cpp \
	src/dirtyjtag/protocol.cpp \
	src/swd/ch32_pioc_swd.cpp \
	src/swd/cmsis_dap.cpp \
	src/usb/usb_dirtyjtag.cpp

WCHLINK_SOURCES := \
	src/main_wchlink.cpp \
	src/wchlink/ch32_pioc_rvswd.cpp \
	src/wchlink/ch32_pioc_rvswio.cpp \
	src/wchlink/ch32_wch_autoport.cpp \
	src/wchlink/protocol.cpp \
	src/usb/usb_wchlink.cpp

WCH_PROFILES := wchlink wchlink-rvswio wchlink-rvswio-emit \
	wchlink-rvswd wchlink-rvswd-emit
VALID_PROFILES := jtagswd $(WCH_PROFILES)

ifeq ($(PROFILE),jtagswd)
SOURCES := $(COMMON_SOURCES) $(JTAGSWD_SOURCES)
CPPFLAGS_PROFILE := -Isrc/dirtyjtag -Isrc/swd -Isrc/usb
else ifneq ($(filter $(PROFILE),$(WCH_PROFILES)),)
SOURCES := $(COMMON_SOURCES) $(WCHLINK_SOURCES)
CPPFLAGS_PROFILE := -Isrc/wchlink -Isrc/usb
else
$(error Unknown PROFILE '$(PROFILE)'; expected one of: $(VALID_PROFILES))
endif

ifneq ($(filter $(PROFILE),wchlink-rvswio wchlink-rvswio-emit),)
CPPFLAGS_PROFILE += -DWCH_TRANSPORT_RVSWIO
endif
ifneq ($(filter $(PROFILE),wchlink-rvswd wchlink-rvswd-emit),)
CPPFLAGS_PROFILE += -DWCH_TRANSPORT_RVSWD
endif
ifneq ($(filter $(PROFILE),wchlink-rvswio-emit wchlink-rvswd-emit),)
CPPFLAGS_PROFILE += -DWCH_EMIT_SELFTEST
endif

OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SOURCES)) \
	$(BUILD_DIR)/sdk/ch32fun/ch32fun/ch32fun.o
DEPS := $(OBJECTS:.o=.d)

PIOC_ASM := pioc/tapioca_swd.ASM pioc/tapioca_rvswio.ASM pioc/tapioca_rvswd.ASM

.DEFAULT_GOAL := all

.PHONY: all firmware help jtagswd $(WCH_PROFILES) minichlink probe-wchlink \
	flash-jtagswd flash-wchlink verify-pioc regenerate-pioc test clean distclean \
	check-tools check-submodules

all: jtagswd wchlink

help:
	@printf '%s\n' \
		'make all             Build both firmwares and host minichlink' \
		'make jtagswd         Build the JTAG + CMSIS-DAP firmware' \
		'make wchlink         Build the auto-detecting WCH-Link firmware' \
		'make flash-jtagswd   Build and flash JTAG/SWD through USB ISP' \
		'make flash-wchlink   Build and flash WCH-Link through USB ISP' \
		'make minichlink      Build the pinned host-side minichlink' \
		'make probe-wchlink   Identify a WCH target through the probe' \
		'make test            Build and run all host-side tests' \
		'make verify-pioc     Check that generated PIOC headers are current'

jtagswd $(WCH_PROFILES):
	@$(MAKE) --no-print-directory PROFILE=$@ firmware

$(WCH_PROFILES): minichlink

firmware: $(TARGET).bin

$(OBJECTS): | check-tools check-submodules

$(TARGET).elf: $(OBJECTS) $(LINKER_SCRIPT) | verify-pioc
	@mkdir -p $(@D)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS_PROFILE) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/sdk/ch32fun/ch32fun/ch32fun.o: $(CH32FUN)/ch32fun.c src/funconfig.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS_PROFILE) $(CFLAGS) -Wno-unused-parameter -c $< -o $@

check-tools:
	@test -n "$(RISCV_PREFIX)" || { \
		echo "No compatible RISC-V toolchain: set RISCV_PREFIX, install sdk/toolchain (see README), or add one to PATH." >&2; exit 1; }
	@for tool in "$(CC)" "$(CXX)" "$(OBJCOPY)" "$(SIZE)"; do \
		command -v "$$tool" >/dev/null 2>&1 || { echo "Missing $$tool" >&2; exit 1; }; \
	done
	@$(CC) $(ARCH_FLAGS) -x c -c /dev/null -o /dev/null >/dev/null 2>&1 || { \
		echo "$(CC) does not support the required WCH rv32imacxw target." >&2; exit 1; }

check-submodules:
	@test -f "$(CH32FUN)/ch32fun.c" || { \
		echo "Missing sdk/ch32fun; run: git submodule update --init" >&2; exit 1; }

verify-pioc:
	@for source in $(PIOC_ASM); do $(PYTHON) pioc/assemble.py $$source || exit; done

regenerate-pioc:
	@for source in $(PIOC_ASM); do $(PYTHON) pioc/assemble.py $$source --write || exit; done

flash-jtagswd: jtagswd
	$(WCHISP) flash $(BUILD_ROOT)/jtagswd/$(PROJECT)-jtagswd.bin

flash-wchlink: wchlink
	$(WCHISP) flash $(BUILD_ROOT)/wchlink/$(PROJECT)-wchlink.bin

minichlink: check-submodules
	$(MAKE) -C $(MINICHLINK_DIR) minichlink

probe-wchlink: minichlink
	$(MINICHLINK) -C linke -i

HOST_TESTS := packet_order_test pioc_swd_protocol_test wch_rvswd_frame_test \
	wch_link_fixtures_test cmsis_dap_test protocol_test wch_link_protocol_test

test: $(addprefix $(BUILD_ROOT)/tests/,$(HOST_TESTS))
	@for test_bin in $^; do $$test_bin || exit; done

$(BUILD_ROOT)/tests/packet_order_test: tests/packet_order_test.cpp
$(BUILD_ROOT)/tests/pioc_swd_protocol_test: tests/pioc_swd_protocol_test.cpp
$(BUILD_ROOT)/tests/wch_rvswd_frame_test: tests/wch_rvswd_frame_test.cpp
$(BUILD_ROOT)/tests/wch_link_fixtures_test: tests/wch_link_fixtures_test.cpp
$(BUILD_ROOT)/tests/cmsis_dap_test: tests/cmsis_dap_test.cpp src/swd/cmsis_dap.cpp
$(BUILD_ROOT)/tests/protocol_test: tests/protocol_test.cpp src/dirtyjtag/protocol.cpp
$(BUILD_ROOT)/tests/wch_link_protocol_test: tests/wch_link_protocol_test.cpp src/wchlink/protocol.cpp

$(BUILD_ROOT)/tests/%:
	@mkdir -p $(@D)
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Isrc -Itests $^ -o $@

clean:
	rm -rf $(BUILD_ROOT)

distclean: clean

-include $(DEPS)
