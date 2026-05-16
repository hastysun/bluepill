# STM32F103 USB Button Box (libopencm3)

32-button USB HID joystick firmware for the Blue Pill (STM32F103C8T6).
Multiple **variants** share one codebase: the pin map, encoder count,
USB PID, and product string all live in a small per-variant header
under `variants/`, and `make VARIANT=<name>` selects one. Every encoder
sits on a contiguous 3-pin block of the header so wiring stays tidy.
Uses libopencm3 — no HAL, no CubeMX, no IDE required.

---

## Toolchain Setup

### macOS
```bash
brew install arm-none-eabi-gcc stlink
```

### Linux (Debian/Ubuntu)
```bash
sudo apt install gcc-arm-none-eabi stlink-tools
```

### Windows
Install [GNU Arm Embedded Toolchain](https://developer.arm.com/downloads/-/gnu-rm) and [stlink](https://github.com/stlink-org/stlink/releases). Add both to your PATH.

---

## Quick Start

```bash
# 1. Clone the repo and pull in libopencm3
git clone https://github.com/hastysun/bluepill
cd bluepill
git submodule update --init

# 2. Build libopencm3 (one-time, takes ~1 minute)
make -C libopencm3 TARGETS=stm32/f1

# 3. Build firmware (default variant)
make

# 4. Connect ST-Link, then flash
make flash
```

Build output lives in `build/<variant>/button_box.{elf,bin,hex}` so
different variants don't overwrite each other.

---

## Variants

Three pre-built configurations are available. Select one with `VARIANT=`:

| Variant          | Buttons | Encoders | USB product string  | PID    | Pinout                    |
|------------------|---------|----------|---------------------|--------|---------------------------|
| `default`        | 7       | 6        | `STM32 Button Box`  | 0x0001 | `pinout-default.svg`      |
| `all_encoders`   | 4       | 7        | `STM32 Encoder Box` | 0x0002 | `pinout-all_encoders.svg` |
| `three_encoders` | 16      | 3        | `STM32 Mixed Box`   | 0x0003 | `pinout-three_encoders.svg` |

```bash
# Build and flash a specific variant
make VARIANT=three_encoders
make VARIANT=three_encoders flash

# Build every variant at once (useful after changing main.c)
make all-variants
```

> **Why distinct PID + product string?** Windows DirectInput and DCS World
> key their bindings by USB VID + PID. Two variants with the same PID
> would be treated as the same device, so their button maps would collide.
> Pick a free PID from [pid.codes](https://pid.codes/) (VID `0x1209`) if
> you publish or share your firmware.

---

## Wiring

All inputs use internal pull-ups — wire each button or encoder pin to **GND**. No external resistors needed.

### Buttons

Connect each button between the listed pin and GND. Active-low; 5 ms debounce built in.

#### `default` variant (7 buttons)

| Button | Pin  |  | Button | Pin  |
|--------|------|--|--------|------|
| 1      | PB0  |  | 5      | PB10 |
| 2      | PB1  |  | 6      | PB11 |
| 3      | PB8  |  | 7      | PB15 |
| 4      | PB9  |  |        |      |

#### `three_encoders` variant (16 buttons)

| Button | Pin  |  | Button | Pin  |
|--------|------|--|--------|------|
| 1      | PB0  |  |  9     | PB9  |
| 2      | PB1  |  | 10     | PB10 |
| 3      | PB3  |  | 11     | PB11 |
| 4      | PB4  |  | 12     | PB15 |
| 5      | PB5  |  | 13     | PA0  |
| 6      | PB6  |  | 14     | PA1  |
| 7      | PB7  |  | 15     | PA2  |
| 8      | PB8  |  | 16     | PA15 |

#### `all_encoders` variant (4 buttons)

| Button | Pin  |
|--------|------|
| 1      | PB0  |
| 2      | PB8  |
| 3      | PB9  |
| 4      | PB15 |

### Rotary Encoders

Each encoder has three wires: A, B, and Push (the built-in push switch). Wire each to GND through the encoder. The three pins for each encoder sit adjacent on the header so the wires bundle naturally.

A full detent click sends a 50 ms CW or CCW button pulse. The push button is debounced like a regular button.

#### `default` variant (6 encoders)

| Encoder | A    | B    | Push | HID buttons (CW / CCW / Push) |
|---------|------|------|------|-------------------------------|
| 1       | PA8  | PA9  | PA10 |  8 /  9 / 10                  |
| 2       | PB12 | PB13 | PB14 | 11 / 12 / 13                  |
| 3       | PA5  | PA6  | PA7  | 14 / 15 / 16                  |
| 4       | PA0  | PA1  | PA2  | 17 / 18 / 19                  |
| 5       | PA15 | PB3  | PB4  | 20 / 21 / 22                  |
| 6       | PB5  | PB6  | PB7  | 23 / 24 / 25                  |

#### `three_encoders` variant (3 encoders)

| Encoder | A    | B    | Push | HID buttons (CW / CCW / Push) |
|---------|------|------|------|-------------------------------|
| 1       | PA8  | PA9  | PA10 | 17 / 18 / 19                  |
| 2       | PB12 | PB13 | PB14 | 20 / 21 / 22                  |
| 3       | PA5  | PA6  | PA7  | 23 / 24 / 25                  |

#### `all_encoders` variant (7 encoders)

| Encoder | A    | B    | Push | HID buttons (CW / CCW / Push) |
|---------|------|------|------|-------------------------------|
| 1       | PA8  | PA9  | PA10 |  5 /  6 /  7                  |
| 2       | PB12 | PB13 | PB14 |  8 /  9 / 10                  |
| 3       | PA5  | PA6  | PA7  | 11 / 12 / 13                  |
| 4       | PA0  | PA1  | PA2  | 14 / 15 / 16                  |
| 5       | PA15 | PB3  | PB4  | 17 / 18 / 19                  |
| 6       | PB5  | PB6  | PB7  | 20 / 21 / 22                  |
| 7       | PB11 | PB10 | PB1  | 23 / 24 / 25                  |

### Fixed pins (all variants)

| Function        | Pin(s)                      |
|-----------------|-----------------------------|
| LED 1 (USB OK)  | PA3                         |
| LED 2 (encoder) | PA4                         |
| Heartbeat LED   | PB2 (WeAct Blue Pill Plus)  |
| USB D− / D+     | PA11 / PA12                 |
| SWD debug       | PA13 (SWDIO) / PA14 (SWCLK) |

> **Note:** JTAG is disabled at startup to free PA15, PB3, and PB4 as GPIO. SWD debugging still works on PA13/PA14.

---

## USB Enumeration

The WeAct Blue Pill Plus has the correct 1.5 kΩ D+ pull-up and enumerates
reliably. If you're using an original Blue Pill with a 10 kΩ resistor (R10),
either replace R10 with 1.5 kΩ or uncomment `usb_force_reset()` in `main.c`.

---

## Testing the Device

Once flashed, verify the device is working:

- **Linux**: `jstest /dev/input/js0` or `evtest`
- **Windows**: Control Panel → Devices and Printers → right-click the device → Game controller settings → Properties
- **macOS**: Joystick Doctor (free on the App Store)

Buttons are numbered from 1 in most software. Rotate an encoder knob — you should see two buttons (CW and CCW) pulse briefly per detent. Press the encoder shaft to see the push button.

---

## Adding a New Variant

1. Copy an existing variant header:
   ```bash
   cp variants/default.h variants/my_variant.h
   ```

2. Edit `variants/my_variant.h`:
   - Set a unique `DEVICE_PID` (e.g. `0x0004`)
   - Set a unique `DEVICE_PRODUCT_STR` (e.g. `"STM32 My Box"`)
   - Update `NUM_BUTTONS` and `NUM_ENCODERS`
   - Edit `variant_button_pins[]` and `variant_encoder_defs[]`

3. Build and flash:
   ```bash
   make VARIANT=my_variant
   make VARIANT=my_variant flash
   ```

4. Add it to the variants table in this README and in `CLAUDE.md`.

---

## Project Structure

```
├── Makefile                  # Build + flash targets (VARIANT= selector)
├── stm32f103c8t6.ld          # Linker script (64K flash, 20K RAM)
├── main.c                    # Variant-agnostic firmware
├── button_box.h              # Shared types (pin_t, encoder_def_t)
├── variants/
│   ├── default.h             # 7 buttons + 6 encoders
│   ├── all_encoders.h        # 4 buttons + 7 encoders
│   └── three_encoders.h      # 16 buttons + 3 encoders
├── pinout-default.svg        # Board diagram — default variant
├── pinout-all_encoders.svg   # Board diagram — all_encoders variant
└── libopencm3/               # Git submodule (MCU peripheral library)
```

## Tuning

All timing constants are at the top of `main.c`:

| Constant        | Default | Effect                                              |
|-----------------|---------|-----------------------------------------------------|
| `DEBOUNCE_MS`   | 5 ms    | Button noise filter — increase if buttons chatter   |
| `ENC_PULSE_MS`  | 50 ms   | How long each encoder click registers as a button press |
| `ENC_GAP_MS`    | 15 ms   | Gap between pulses when spinning fast               |
| `LED_BLINK_MS`  | 500 ms  | Heartbeat LED half-period                           |
