# STM32F103 Button Box — libopencm3 Makefile
#
# Usage:
#   make            Build firmware
#   make flash      Flash via ST-Link
#   make clean      Remove build artifacts
#
# Prerequisites:
#   - arm-none-eabi-gcc toolchain
#   - libopencm3 (git submodule or set OPENCM3_DIR)
#   - stlink tools (st-flash)

# Project
PROJECT  = button_box
BUILDDIR = build

# Toolchain
PREFIX  ?= arm-none-eabi
CC       = $(PREFIX)-gcc
OBJCOPY  = $(PREFIX)-objcopy
OBJDUMP  = $(PREFIX)-objdump
SIZE     = $(PREFIX)-size
STFLASH  = st-flash

# libopencm3 — adjust if installed elsewhere
OPENCM3_DIR ?= libopencm3

# Target MCU
DEVICE   = stm32f103c8t6
LDSCRIPT = stm32f103c8t6.ld

# Sources — add any new .c files here
SRCS = main.c
OBJS = $(SRCS:%.c=$(BUILDDIR)/%.o)

# Flags
ARCH_FLAGS = -mthumb -mcpu=cortex-m3 -msoft-float

CFLAGS  = -Os -g -Wall -Wextra \
          -fno-common -ffunction-sections -fdata-sections \
          $(ARCH_FLAGS) \
          -DSTM32F1 \
          -I$(OPENCM3_DIR)/include \
          -std=c99

LDFLAGS = $(ARCH_FLAGS) \
          -L$(OPENCM3_DIR)/lib \
          -T$(LDSCRIPT) \
          -Wl,--gc-sections \
          -nostartfiles \
          -Wl,--print-memory-usage

LDLIBS  = -lopencm3_stm32f1 -lc -lgcc -lnosys

# Rules
.PHONY: all flash clean size

all: $(BUILDDIR)/$(PROJECT).bin $(BUILDDIR)/$(PROJECT).hex size

$(BUILDDIR)/$(PROJECT).elf: $(OBJS) $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a | $(BUILDDIR)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/$(PROJECT).bin: $(BUILDDIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILDDIR)/$(PROJECT).hex: $(BUILDDIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

size: $(BUILDDIR)/$(PROJECT).elf
	$(SIZE) $<

flash: $(BUILDDIR)/$(PROJECT).bin
	$(STFLASH) write $< 0x08000000

clean:
	rm -rf $(BUILDDIR)

# Build libopencm3 if needed
$(OPENCM3_DIR)/lib/libopencm3_stm32f1.a:
	$(MAKE) -C $(OPENCM3_DIR) TARGETS=stm32/f1
