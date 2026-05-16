# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

USB HID joystick firmware for the STM32F103C8T6 (Blue Pill). Enumerates as a 32-button joystick for flight sim button boxes (e.g., DCS World). 7 momentary inputs plus 6 rotary encoders (A/B/Push each). Every encoder's three pins are on a contiguous 3-pin block of the header so the wiring loom stays clean.

## Commands

```bash
make                          # Build default variant → build/default/button_box.{elf,bin,hex}
make VARIANT=all_encoders     # Build a named variant → build/all_encoders/...
make flash                    # Flash via ST-Link (respects VARIANT)
make size                     # Show flash/RAM usage
make clean                    # Remove every variant's build dir
```

The variant selects the pin map, encoder count, USB PID, and product string. Each variant lives as a single header in `variants/`.

First build also compiles libopencm3: `make -C libopencm3 TARGETS=stm32/f1`

**Toolchain required:** `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, `st-flash`

## Architecture

Single-file firmware (`main.c`) targeting ARM Cortex-M3. No RTOS — bare-metal main loop polling GPIO and sending USB HID reports. **Variant-agnostic**: the pin map and USB identity are injected at compile time via a header in `variants/`.

**Core subsystems in `main.c`:**

- **Variant selection** — Makefile sets `-DVARIANT_CONFIG="variants/<name>.h"`; `main.c` includes that header, which provides `NUM_BUTTONS`, `NUM_ENCODERS`, `variant_button_pins[]`, `variant_encoder_defs[]`, `DEVICE_PID`, and `DEVICE_PRODUCT_STR`.
- **Button scanning** — active-low with internal pull-ups, 5ms debounce via hysteresis. Pin list per-variant.
- **Rotary encoders** — array-driven (`variant_encoder_defs[]`), 4-state transition table, emits 50ms button pulses per detent. Encoder count and pins per-variant.
- **USB HID** — VID 0x1209 (shared), PID + product string per-variant, 32-button report (4 bytes) via interrupt endpoint, built on libopencm3.
- **Status LEDs** — PA3 (USB connected), PA4 (any encoder activity), PB2 (heartbeat). Same across every variant.
- **Timing** — 1ms SysTick counter; all debounce/pulse/blink logic uses non-blocking comparisons.

**Existing variants** (`variants/<name>.h`):
- `default` — 7 buttons + 6 encoders, PID 0x0001, product `STM32 Button Box`.
- `all_encoders` — 4 buttons + 7 encoders, PID 0x0002, product `STM32 Encoder Box`.

**Key constants in `main.c`:**
- `HID_NUM_BUTTONS = 32` — buttons exposed in the HID descriptor (4-byte report).
- `DEBOUNCE_MS = 5`, `ENC_PULSE_MS = 50`, `LED_BLINK_MS = 500`.

**Always-reserved pins** (every variant): PA11/PA12 (USB), PA13/PA14 (SWD), PA3/PA4 (LEDs), PB2 (heartbeat). JTAG is disabled at boot so PA15/PB3/PB4 are free as GPIO.

## File Layout

| File | Purpose |
|------|---------|
| `main.c` | Variant-agnostic firmware — clocks, GPIO, USB HID, debounce, encoder |
| `button_box.h` | Shared types (`pin_t`, `encoder_def_t`) used by main.c + every variant |
| `variants/<name>.h` | Per-variant config: pin arrays, encoder count, USB PID + product string |
| `pinout-<name>.svg` | Labelled board diagram per variant |
| `stm32f103c8t6.ld` | Linker script (64K flash / 20K RAM) |
| `libopencm3/` | Git submodule — MCU peripheral library |

## libopencm3

Low-level hardware abstraction (no CubeMX/HAL). Headers are in `libopencm3/include/`, the static library (`libopencm3_stm32f1.a`) is built from source on first `make`. Re-run `make -C libopencm3 TARGETS=stm32/f1` if headers change.
