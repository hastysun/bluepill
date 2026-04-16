# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

USB HID joystick firmware for the STM32F103C8T6 (Blue Pill). Enumerates as a 24-button joystick for flight sim button boxes (e.g., DCS World).

## Commands

```bash
make              # Build firmware → build/button_box.{elf,bin,hex}
make flash        # Flash to device via ST-Link (st-flash write)
make size         # Show flash/RAM usage
make clean        # Remove build artifacts
```

First build also compiles libopencm3: `make -C libopencm3 TARGETS=stm32/f1`

**Toolchain required:** `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, `st-flash`

## Architecture

Single-file firmware (`main.c`) targeting ARM Cortex-M3. No RTOS — bare-metal main loop polling GPIO and sending USB HID reports.

**Core subsystems in `main.c`:**

- **Button scanning** — Port B pins (PB0-PB1, PB3-PB15, PA8), active-low with internal pull-ups, 5ms debounce via hysteresis
- **Rotary encoder** — PA9/PA10 (quadrature), PA15 (push); 4-state transition table, emits 50ms button pulses per detent
- **Ignition switch decoder** — PB12/PB13/PB14 3-way switch decoded into button events (ACC/ON/START)
- **USB HID** — VID 0x1209 / PID 0x0001, 24-button report (3 bytes) via interrupt endpoint, built on libopencm3
- **Status LEDs** — PA3 (USB connected), PA4 (encoder activity), PB2 (heartbeat)
- **Timing** — 1ms SysTick counter; all debounce/pulse/blink logic uses non-blocking comparisons

**Key constants** (top of `main.c`, easy to tune):
- `NUM_BUTTONS = 19` — physical GPIO inputs only (16 buttons + 3 ignition lines). The rotary encoder (CW/CCW/push) is decoded separately and mapped to additional HID button bits.
- `HID_NUM_BUTTONS = 24` — buttons exposed in the HID descriptor (22 currently wired, bits 22–23 reserved).
- `DEBOUNCE_MS = 5`, `ENC_PULSE_MS = 50`, `LED_BLINK_MS = 500`

## File Layout

| File | Purpose |
|------|---------|
| `main.c` | Firmware — all logic in one file |
| `stm32f103c8t6.ld` | Linker script (64K flash / 20K RAM) |
| `libopencm3/` | Git submodule — MCU peripheral library |

## libopencm3

Low-level hardware abstraction (no CubeMX/HAL). Headers are in `libopencm3/include/`, the static library (`libopencm3_stm32f1.a`) is built from source on first `make`. Re-run `make -C libopencm3 TARGETS=stm32/f1` if headers change.
