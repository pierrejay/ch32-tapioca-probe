PROJECT := tapioca-probe
PROFILE ?= jtagswd

# Supported firmware options. They may all be passed on the same make command
# line as the build or flash target; see `make help`.
BOARD ?= reference
UART_BRIDGE ?= 0
JTAG_TRST ?= 0

ifneq ($(UART_BRIDGE),0)
ifneq ($(UART_BRIDGE),1)
$(error UART_BRIDGE must be 0 or 1)
endif
endif
ifneq ($(JTAG_TRST),0)
ifneq ($(JTAG_TRST),1)
$(error JTAG_TRST must be 0 or 1)
endif
endif

ifeq ($(BOARD),reference)
BOARD_SUFFIX :=
BOARD_CPPFLAGS :=
else ifeq ($(BOARD),weact)
BOARD_SUFFIX := -weact
BOARD_CPPFLAGS := -DLED_PIN=PB12
else
$(error Unknown BOARD '$(BOARD)'; expected one of: reference weact)
endif

CH32FUN := sdk/ch32fun/ch32fun
MINICHLINK_DIR := sdk/ch32fun/minichlink
MINICHLINK := $(MINICHLINK_DIR)/minichlink
LINKER_SCRIPT := ldscript/Link_CH32X035_pioc.ld
BUILD_ROOT := build
UART_SUFFIX := $(if $(filter 1,$(UART_BRIDGE)),-uart,)
TRST_SUFFIX := $(if $(filter 1,$(JTAG_TRST)),-trst,)
FEATURE_SUFFIX := $(UART_SUFFIX)$(TRST_SUFFIX)
BUILD_NAME := $(PROFILE)$(BOARD_SUFFIX)$(FEATURE_SUFFIX)
BUILD_DIR := $(BUILD_ROOT)/$(BUILD_NAME)
TARGET := $(BUILD_DIR)/$(PROJECT)-$(BUILD_NAME)

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
HOST_CC ?= cc
HOST_CXXFLAGS ?= -std=c++17 -Wall -Wextra
PYTHON ?= python3
WCHISP ?= wchisp
EXTRA_CPPFLAGS ?=
LIBUSB_CFLAGS ?= $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS ?= $(shell pkg-config --libs libusb-1.0 2>/dev/null)

CODEGEN_FLAGS := $(ARCH_FLAGS) -Os -g -flto -ffunction-sections -fdata-sections \
	-fmessage-length=0 -msmall-data-limit=8 -msave-restore -fsigned-char \
	-fno-common
COMMON_FLAGS := $(CODEGEN_FLAGS) -Wall -Wextra -MMD -MP \
	-DCH32X035F8 -DCH32X03X -DCH32X03x -DCH32X035 \
	-DFUNCONF_ENABLE_HPE=1 \
	-Isrc -Isrc/hal -I$(CH32FUN) $(BOARD_CPPFLAGS) $(EXTRA_CPPFLAGS)
ifeq ($(UART_BRIDGE),1)
COMMON_FLAGS += -DUART_BRIDGE=1
endif
ifeq ($(JTAG_TRST),1)
COMMON_FLAGS += -DJTAG_TRST=1
endif
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

ifeq ($(UART_BRIDGE),1)
COMMON_SOURCES += src/uart_bridge.cpp
endif

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

ifeq ($(JTAG_TRST),1)
ifneq ($(PROFILE),jtagswd)
$(error JTAG_TRST=1 is only valid for the jtagswd firmware)
endif
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
	wchlink-diag diagnose-wchlink \
	flash-jtagswd flash-wchlink verify-pioc regenerate-pioc test clean distclean \
	check-tools check-submodules

all: jtagswd wchlink

help:
	@printf '%s\n' \
		'Usage:' \
		'  make [OPTIONS] <target>' \
		'' \
		'Firmware options (defaults shown):' \
		'  BOARD=reference       reference | weact (WeAct uses LED PB12)' \
		'  UART_BRIDGE=0          1 adds the PB0/PB1 USB CDC UART bridge' \
		'  JTAG_TRST=0            1 routes physical JTAG nTRST to PA5 (jtagswd only)' \
		'  RISCV_PREFIX=<prefix>  override RISC-V toolchain auto-detection' \
		'  EXTRA_CPPFLAGS=<flags> advanced custom board/compiler definitions' \
		'' \
		'Build and flash:' \
		'  make all               build both default firmwares and host minichlink' \
		'  make jtagswd           build the JTAG + CMSIS-DAP firmware' \
		'  make wchlink           build the auto-detecting WCH-Link firmware' \
		'  make flash-jtagswd     build and flash JTAG/SWD through USB ISP' \
		'  make flash-wchlink     build and flash WCH-Link through USB ISP' \
		'  make BOARD=weact UART_BRIDGE=1 JTAG_TRST=1 flash-jtagswd' \
		'' \
		'Other targets:' \
		'  make minichlink        build the pinned host-side minichlink' \
		'  make probe-wchlink     identify a WCH target through the probe' \
		'  make diagnose-wchlink  read latched WCH transport diagnostics' \
		'  make test              build and run all host-side tests' \
		'  make verify-pioc       check that generated PIOC headers are current' \
		'  make regenerate-pioc   regenerate PIOC headers from their ASM sources' \
		'  make clean             remove all generated build files' \
		'' \
		'Supported tool/action overrides:' \
		'  WCHISP=<command> PYTHON=<command> HOST_CC=<command> HOST_CXX=<command>' \
		'  HOST_CXXFLAGS=<flags> LIBUSB_CFLAGS=<flags> LIBUSB_LIBS=<flags>' \
		'  SERIAL=<serial> DIAG_ARGS=<arguments>   (diagnose-wchlink only)'

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
	$(WCHISP) flash $(BUILD_ROOT)/jtagswd$(BOARD_SUFFIX)$(FEATURE_SUFFIX)/$(PROJECT)-jtagswd$(BOARD_SUFFIX)$(FEATURE_SUFFIX).bin

flash-wchlink: wchlink
	$(WCHISP) flash $(BUILD_ROOT)/wchlink$(BOARD_SUFFIX)$(FEATURE_SUFFIX)/$(PROJECT)-wchlink$(BOARD_SUFFIX)$(FEATURE_SUFFIX).bin

minichlink: check-submodules
	$(MAKE) -C $(MINICHLINK_DIR) minichlink

probe-wchlink: minichlink
	$(MINICHLINK) -C linke -i

WCHLINK_DIAG := $(BUILD_ROOT)/host/wchlink-diag

wchlink-diag: $(WCHLINK_DIAG)

diagnose-wchlink: wchlink-diag
	$(WCHLINK_DIAG) $(SERIAL) $(DIAG_ARGS)

$(WCHLINK_DIAG): sdk/wchlink_diag.c
	@mkdir -p $(@D)
	@test -n "$(LIBUSB_LIBS)" || { echo "libusb-1.0 development files not found by pkg-config." >&2; exit 1; }
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra $(LIBUSB_CFLAGS) $< $(LIBUSB_LIBS) -o $@

HOST_TESTS := packet_order_test pioc_swd_protocol_test wch_rvswd_frame_test \
	wch_link_fixtures_test cmsis_dap_test protocol_test wch_link_protocol_test \
	usb_descriptors_test
HOST_TEST_BINS := $(addprefix $(BUILD_ROOT)/tests/,$(HOST_TESTS))
HOST_TEST_OBJECTS := \
	$(addprefix $(BUILD_ROOT)/tests/obj/tests/,$(addsuffix .o,$(HOST_TESTS))) \
	$(BUILD_ROOT)/tests/obj/src/swd/cmsis_dap.o \
	$(BUILD_ROOT)/tests/obj/src/dirtyjtag/protocol.o \
	$(BUILD_ROOT)/tests/obj/src/wchlink/protocol.o
HOST_TEST_DEPS := $(HOST_TEST_OBJECTS:.o=.d)

test: $(HOST_TEST_BINS)
	@for test_bin in $^; do $$test_bin || exit; done

$(BUILD_ROOT)/tests/packet_order_test: $(BUILD_ROOT)/tests/obj/tests/packet_order_test.o
$(BUILD_ROOT)/tests/pioc_swd_protocol_test: $(BUILD_ROOT)/tests/obj/tests/pioc_swd_protocol_test.o
$(BUILD_ROOT)/tests/wch_rvswd_frame_test: $(BUILD_ROOT)/tests/obj/tests/wch_rvswd_frame_test.o
$(BUILD_ROOT)/tests/wch_link_fixtures_test: $(BUILD_ROOT)/tests/obj/tests/wch_link_fixtures_test.o
$(BUILD_ROOT)/tests/cmsis_dap_test: $(BUILD_ROOT)/tests/obj/tests/cmsis_dap_test.o \
	$(BUILD_ROOT)/tests/obj/src/swd/cmsis_dap.o
$(BUILD_ROOT)/tests/protocol_test: $(BUILD_ROOT)/tests/obj/tests/protocol_test.o \
	$(BUILD_ROOT)/tests/obj/src/dirtyjtag/protocol.o
$(BUILD_ROOT)/tests/wch_link_protocol_test: $(BUILD_ROOT)/tests/obj/tests/wch_link_protocol_test.o \
	$(BUILD_ROOT)/tests/obj/src/wchlink/protocol.o
$(BUILD_ROOT)/tests/usb_descriptors_test: $(BUILD_ROOT)/tests/obj/tests/usb_descriptors_test.o

$(HOST_TEST_BINS):
	@mkdir -p $(@D)
	$(HOST_CXX) $(HOST_CXXFLAGS) $^ -o $@

$(BUILD_ROOT)/tests/obj/%.o: %.cpp
	@mkdir -p $(@D)
	$(HOST_CXX) $(HOST_CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -Isrc -Itests \
		-c $< -o $@

clean:
	rm -rf $(BUILD_ROOT)

distclean: clean

-include $(DEPS) $(HOST_TEST_DEPS)
