# STM32F103 Button Box — libopencm3 Makefile
#
# Usage:
#   make                          Build the default variant
#   make VARIANT=all_encoders     Build a named variant from variants/<name>.h
#   make flash                    Flash via ST-Link (uses VARIANT)
#   make clean                    Remove every variant's build output
#
# Each variant builds into build/<variant>/ so artifacts for
# different variants don't collide. Add a new variant by creating
# variants/<name>.h — see variants/default.h for the template.
#
# Prerequisites:
#   - arm-none-eabi-gcc toolchain
#   - libopencm3 (git submodule or set OPENCM3_DIR)
#   - stlink tools (st-flash)

# Project
PROJECT  = button_box

# Variant — pin map / USB identity selector. Default matches the
# original 7-button / 6-encoder layout.
VARIANT  ?= default
BUILDDIR  = build/$(VARIANT)

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
          -MMD -MP \
          $(ARCH_FLAGS) \
          -DSTM32F1 \
          -DVARIANT_CONFIG=\"variants/$(VARIANT).h\" \
          -I$(OPENCM3_DIR)/include \
          -I. \
          -std=c99

LDFLAGS = $(ARCH_FLAGS) \
          -L$(OPENCM3_DIR)/lib \
          -T$(LDSCRIPT) \
          -Wl,--gc-sections \
          -Wl,-Map=$(BUILDDIR)/$(PROJECT).map \
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
	rm -rf build

# Build libopencm3 if needed
$(OPENCM3_DIR)/lib/libopencm3_stm32f1.a:
	$(MAKE) -C $(OPENCM3_DIR) TARGETS=stm32/f1

# Auto-generated header dependencies (from -MMD -MP)
-include $(OBJS:.o=.d)
