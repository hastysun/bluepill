# STM32F103 USB Button Box (libopencm3)

32-button USB HID joystick firmware for the Blue Pill (STM32F103C8T6).
Multiple **variants** share one codebase: the pin map, encoder count,
USB PID, and product string all live in a small per-variant header
under `variants/`, and `make VARIANT=<name>` selects one. Every encoder
sits on a contiguous 3-pin block of the header so wiring stays tidy.
Uses libopencm3 — no HAL, no CubeMX, no IDE required.

## Quick Start

```bash
# Clone with libopencm3 as submodule
git clone <your-repo>
cd encoderpill
git submodule add https://github.com/libopencm3/libopencm3.git
cd libopencm3 && make TARGETS=stm32/f1 && cd ..

# Build the default variant
make
make flash

# Build a different variant
make VARIANT=all_encoders
make VARIANT=all_encoders flash
```

Build output lives in `build/<variant>/button_box.{elf,bin,hex}` so
different variants don't clobber each other.

## Requirements

- `arm-none-eabi-gcc` toolchain
- `stlink` tools (`st-flash`)
- libopencm3 (as submodule or set `OPENCM3_DIR=/path/to/libopencm3`)

## Variants

| Variant         | Buttons | Encoders | USB product string  | PID    | Pinout                       |
|-----------------|---------|----------|---------------------|--------|------------------------------|
| `default`       | 7       | 6        | `STM32 Button Box`  | 0x0001 | `pinout-default.svg`         |
| `all_encoders`  | 4       | 7        | `STM32 Encoder Box` | 0x0002 | `pinout-all_encoders.svg`    |

Each variant has its own header in `variants/`. To add a new variant,
copy `variants/default.h`, change `DEVICE_PID`, `DEVICE_PRODUCT_STR`,
and the `variant_button_pins[]` / `variant_encoder_defs[]` arrays,
then `make VARIANT=<your_name>`.

> **Why distinct PID + product string?** Windows DirectInput / DCS key
> their bindings by USB VID + PID + product string. Two variants with
> the same identity would be treated as the same device by games, so
> their button maps would collide. Pick a free PID from
> [pid.codes](https://pid.codes/) (VID `0x1209`) when publishing.

## Wiring (default variant)

All inputs use internal pull-ups. Wire each button/switch between the pin and **GND**.

| Button | Pin  |  | Button | Pin  |
|--------|------|--|--------|------|
| 1      | PB0  |  | 5      | PB10 |
| 2      | PB1  |  | 6      | PB11 |
| 3      | PB8  |  | 7      | PB15 |
| 4      | PB9  |  |        |      |

Each rotary encoder uses three pins (A, B, push) wired between the pin and GND.
A full detent click emits a brief CW or CCW button pulse. Every encoder's
A/B/Push pins are adjacent on the header so the three wires can run together.

| Encoder | A    | B    | Push | HID buttons (CW / CCW / Push) |
|---------|------|------|------|-------------------------------|
| 1       | PA8  | PA9  | PA10 |  8 /  9 / 10                  |
| 2       | PB12 | PB13 | PB14 | 11 / 12 / 13                  |
| 3       | PA5  | PA6  | PA7  | 14 / 15 / 16                  |
| 4       | PA0  | PA1  | PA2  | 17 / 18 / 19                  |
| 5       | PA15 | PB3  | PB4  | 20 / 21 / 22                  |
| 6       | PB5  | PB6  | PB7  | 23 / 24 / 25                  |

| Function       | Pins                        |
|----------------|-----------------------------|
| LED 1 (USB)    | PA3                         |
| LED 2 (encoder)| PA4                         |
| Heartbeat LED  | PB2 (WeAct Blue Pill Plus)  |
| USB D− / D+    | PA11 / PA12                 |

> **Note:** JTAG is disabled at startup to free PA15, PB3, and PB4 as GPIO. SWD debugging still works.

See `pinout-default.svg` / `pinout-all_encoders.svg` for the labelled board diagram of each variant.

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
├── Makefile              # Build + flash targets (uses VARIANT=)
├── stm32f103c8t6.ld      # Linker script (64K flash, 20K RAM)
├── main.c                # Variant-agnostic: clocks, GPIO, USB HID, debounce, encoder
├── button_box.h          # Shared types (pin_t, encoder_def_t)
├── variants/
│   ├── default.h         # 7 buttons + 6 encoders
│   └── all_encoders.h    # 4 buttons + 7 encoders
├── pinout-default.svg
├── pinout-all_encoders.svg
└── libopencm3/           # Git submodule
```

## Customisation

- **Tune timing**: Adjust `DEBOUNCE_MS` / `ENC_PULSE_MS` in `main.c`
- **New variant**: Copy a file in `variants/`, edit the pin arrays + PID + product string, then `make VARIANT=<name>`
- **Change pins / counts in an existing variant**: Edit `variant_button_pins[]` / `variant_encoder_defs[]` + `NUM_BUTTONS` / `NUM_ENCODERS` in that variant's header
- **VID**: Edit `dev_descr.idVendor` in `main.c` (VID applies to every variant; PID differs per-variant)
