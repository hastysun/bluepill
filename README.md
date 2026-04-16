# STM32F103 USB Button Box (libopencm3)

19-button USB HID joystick firmware for the Blue Pill (STM32F103C8T6).
Uses libopencm3 — no HAL, no CubeMX, no IDE required.

## Quick Start

```bash
# Clone with libopencm3 as submodule
git clone <your-repo>
cd bluepilltest
git submodule add https://github.com/libopencm3/libopencm3.git
cd libopencm3 && make TARGETS=stm32/f1 && cd ..

# Build and flash
make
make flash
```

## Requirements

- `arm-none-eabi-gcc` toolchain
- `stlink` tools (`st-flash`)
- libopencm3 (as submodule or set `OPENCM3_DIR=/path/to/libopencm3`)

## Wiring

All inputs use internal pull-ups. Wire each button/switch between the pin and **GND**.

| Button | Pin  |  | Button | Pin       |
|--------|------|--|--------|-----------|
| 1      | PB0  |  | 11     | PB11      |
| 2      | PB1  |  | 12     | PB12 (IGN ACC)   |
| 3      | PB3  |  | 13     | PB13 (IGN ON)    |
| 4      | PB4  |  | 14     | PB14 (IGN START) |
| 5      | PB5  |  | 15     | PB15      |
| 6      | PB6  |  | 16     | PA8       |
| 7      | PB7  |  | 17     | PA0       |
| 8      | PB8  |  | 18     | PA1       |
| 9      | PB9  |  | 19     | PA2       |
| 10     | PB10 |  |        |           |

| Function       | Pins                        |
|----------------|-----------------------------|
| Encoder A/B    | PA9 / PA10                  |
| Encoder push   | PA15                        |
| LED 1 (USB)    | PA3                         |
| LED 2 (encoder)| PA4                         |
| Heartbeat LED  | PB2 (WeAct Blue Pill Plus)  |
| USB D− / D+    | PA11 / PA12                 |

> **Note:** JTAG is disabled at startup to free PA15, PB3, and PB4 as GPIO. SWD debugging still works.

### Ignition Switch

Buttons 12–14 (PB12–PB14) are decoded as a 3-position ignition switch:

| Switch position | Active pins     | HID button(s) |
|-----------------|-----------------|---------------|
| OFF             | none            | none          |
| ACC             | PB12            | 12            |
| ON              | PB12 + PB13     | 13            |
| START           | PB12–PB14       | 13 + 14       |

## USB Enumeration

The WeAct Blue Pill Plus has the correct 1.5 kΩ D+ pull-up and enumerates
reliably. If you're using an original Blue Pill with a 10 kΩ resistor (R10),
either replace it with 1.5 kΩ or uncomment `usb_force_reset()` in `main.c`.

## Testing

- **Linux**: `jstest /dev/input/js0` or `evtest`
- **Windows**: Control Panel → Game Controllers → Properties
- **macOS**: Joystick Doctor or similar

## Project Structure

```
├── Makefile              # Build + flash targets
├── stm32f103c8t6.ld      # Linker script (64K flash, 20K RAM)
├── main.c                # Everything: clocks, GPIO, USB HID, debounce, encoder
└── libopencm3/           # Git submodule
```

## Customisation

- **Fewer buttons**: Remove entries from `button_pins[]` and update `NUM_BUTTONS`
- **Different pins**: Change port/pin in `button_pins[]`
- **Debounce timing**: Adjust `DEBOUNCE_MS` (default 5 ms)
- **Encoder pulse length**: Adjust `ENC_PULSE_MS` (default 50 ms)
- **VID/PID**: Update `dev_descr` — get a free PID from pid.codes
