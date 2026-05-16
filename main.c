/**
 * main.c — STM32F103 USB HID Button Box (libopencm3)
 *
 * 32-button USB HID joystick firmware. The actual pin map, encoder
 * count, USB product string, and PID all live in a variant header
 * under variants/ — the Makefile selects one via VARIANT=<name>
 * and passes it to the compiler as -DVARIANT_CONFIG="variants/<name>.h".
 *
 * Encoder pulses are ideal for DCS World — each detent click sends
 * a brief button press that DCS can bind to radio tuning, altimeter
 * setting, etc.
 *
 * Buttons are active-low with internal pull-ups: wire each to GND.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/desig.h>
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/stm32/st_usbfs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <stddef.h>

#include "button_box.h"

/* Variant header — pin map, encoder count, USB identity. The
 * Makefile defines VARIANT_CONFIG (e.g. "variants/default.h"). */
#ifndef VARIANT_CONFIG
#define VARIANT_CONFIG "variants/default.h"
#endif
#include VARIANT_CONFIG

/* ============================================================
 *  Configuration
 * ============================================================ */

#define DEBOUNCE_MS       5        /* Button debounce time             */
#define ENC_PULSE_MS      50       /* How long encoder "press" lasts   */
#define ENC_GAP_MS        15       /* Gap between queued encoder pulses*/
#define LED_BLINK_MS      500      /* Onboard LED half-period          */
#define IWDG_PERIOD_MS    1000     /* Independent watchdog timeout     */

/* HID buttons: 3 bytes covers 24, 4 bytes covers 32. With 3 encoders
 * (9 bits) plus 16 raw buttons we need 25 bits → bump to 32. */
#define HID_NUM_BUTTONS   32
#define HID_REPORT_BYTES  ((HID_NUM_BUTTONS + 7) / 8)

/* Catch variants that accidentally overflow the HID report at compile time.
 * Buttons occupy bits 0..NUM_BUTTONS-1; encoders follow sequentially. */
_Static_assert(NUM_BUTTONS + NUM_ENCODERS * 3 <= HID_NUM_BUTTONS,
    "Variant exceeds HID_NUM_BUTTONS — raise HID_NUM_BUTTONS or reduce buttons/encoders");

/* ============================================================
 *  Pin Map — provided by the variant header (variants/<name>.h)
 *
 *  The variant header defines:
 *      NUM_BUTTONS, NUM_ENCODERS
 *      variant_button_pins[]
 *      variant_encoder_defs[]
 *      DEVICE_PID, DEVICE_PRODUCT_STR
 *
 *  Always-reserved pins (used by every variant):
 *      PA11/PA12 = USB D-/D+
 *      PA13/PA14 = SWDIO/SWCLK (debug)
 *      PA3       = LED 1 ("USB connected")
 *      PA4       = LED 2 ("encoder activity")
 *      PB2       = onboard heartbeat LED
 * ============================================================ */

/* LED pins */
#define LED1_PORT    GPIOA
#define LED1_PIN     GPIO3
#define LED2_PORT    GPIOA
#define LED2_PIN     GPIO4

/* ============================================================
 *  SysTick millisecond counter
 * ============================================================ */

static volatile uint32_t systick_ms = 0;

void sys_tick_handler(void)
{
    systick_ms++;
}

static uint32_t millis(void)
{
    return systick_ms;
}

static void systick_init(void)
{
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(72000 - 1);  /* 72 MHz / 72000 = 1 kHz */
    systick_interrupt_enable();
    systick_counter_enable();
}

/* ============================================================
 *  Button Debouncer
 * ============================================================ */

static uint32_t debounce_ts[NUM_BUTTONS];
static uint8_t  raw_state[NUM_BUTTONS];
static uint8_t  stable_state[NUM_BUTTONS];

static uint32_t scan_buttons(void)
{
    uint32_t now = millis();
    uint32_t result = 0;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        uint8_t current = (gpio_get(variant_button_pins[i].port,
                                     variant_button_pins[i].pin) == 0) ? 1 : 0;

        if (current != raw_state[i]) {
            raw_state[i] = current;
            debounce_ts[i] = now;
        } else if (current != stable_state[i]) {
            if ((now - debounce_ts[i]) >= DEBOUNCE_MS)
                stable_state[i] = current;
        }

        if (stable_state[i])
            result |= (1UL << i);
    }

    return result;
}

/* ============================================================
 *  Rotary Encoder — Quadrature Decoder (per-encoder state)
 * ============================================================
 *
 *  Uses a 2-bit state machine on the A/B signals. The decoder
 *  accumulates sub-counts from the transition table; every ±4
 *  sub-counts equals one full detent click, which is queued as
 *  a pending CW or CCW pulse.
 *
 *  Pending pulses drain one at a time: each is emitted for
 *  ENC_PULSE_MS with an ENC_GAP_MS off-period before the next.
 *  This lets the sim see a distinct release edge between clicks
 *  when the knob is spun faster than ENC_PULSE_MS per detent.
 *
 *  Wiring: encoder common to GND, A/B/Push to their assigned pins.
 *  Most encoders with detents produce one full quadrature
 *  cycle per detent (4 state transitions, 1 count).
 */

typedef struct {
    uint8_t  last_state;
    int8_t   sub_count;        /* ±4 per detent before emitting */
    uint8_t  pending_cw;       /* queued CW pulses (cap 255)    */
    uint8_t  pending_ccw;      /* queued CCW pulses (cap 255)   */
    uint32_t cw_off_at;        /* millis() when CW pulse ends   */
    uint32_t ccw_off_at;       /* millis() when CCW pulse ends  */
    uint32_t cw_ready_at;      /* earliest next CW pulse start  */
    uint32_t ccw_ready_at;     /* earliest next CCW pulse start */
    uint32_t sw_debounce_ts;
    uint8_t  sw_raw;
    uint8_t  sw_stable;
} encoder_state_t;

static encoder_state_t enc_state[NUM_ENCODERS];

/*
 * Quadrature state transition table.
 * Index = (prev_state << 2) | curr_state
 * Value: +1 = CW step, -1 = CCW step, 0 = no change / invalid
 */
static const int8_t enc_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static void encoder_init(void)
{
    for (int i = 0; i < NUM_ENCODERS; i++) {
        const encoder_def_t *d = &variant_encoder_defs[i];
        encoder_state_t     *s = &enc_state[i];

        uint8_t a = gpio_get(d->a.port, d->a.pin) ? 1 : 0;
        uint8_t b = gpio_get(d->b.port, d->b.pin) ? 1 : 0;
        s->last_state     = (a << 1) | b;
        s->sub_count      = 0;
        s->pending_cw     = 0;
        s->pending_ccw    = 0;
        s->cw_off_at      = 0;
        s->ccw_off_at     = 0;
        s->cw_ready_at    = 0;
        s->ccw_ready_at   = 0;
        s->sw_debounce_ts = 0;
        s->sw_raw         = 0;
        s->sw_stable      = 0;
    }
}

static uint32_t scan_encoder_one(const encoder_def_t *d,
                                 encoder_state_t *s,
                                 uint32_t now)
{
    uint32_t bits = 0;

    /* --- Quadrature decode: accumulate sub-counts, queue detents --- */
    uint8_t a = gpio_get(d->a.port, d->a.pin) ? 1 : 0;
    uint8_t b = gpio_get(d->b.port, d->b.pin) ? 1 : 0;
    uint8_t curr_state = (a << 1) | b;

    if (curr_state != s->last_state) {
        int8_t dir = enc_table[(s->last_state << 2) | curr_state];
        s->sub_count += dir;
        if (s->sub_count >= 4) {
            if (s->pending_cw < 0xFF) s->pending_cw++;
            s->sub_count = 0;
        } else if (s->sub_count <= -4) {
            if (s->pending_ccw < 0xFF) s->pending_ccw++;
            s->sub_count = 0;
        }
        s->last_state = curr_state;
    }

    /* --- Drive CW pulse from queue --- */
    if ((int32_t)(s->cw_off_at - now) > 0) {
        bits |= (1UL << d->cw_bit);
    } else if (s->pending_cw > 0 &&
               (int32_t)(now - s->cw_ready_at) >= 0) {
        s->pending_cw--;
        s->cw_off_at   = now + ENC_PULSE_MS;
        s->cw_ready_at = s->cw_off_at + ENC_GAP_MS;
        bits |= (1UL << d->cw_bit);
    }

    /* --- Drive CCW pulse from queue --- */
    if ((int32_t)(s->ccw_off_at - now) > 0) {
        bits |= (1UL << d->ccw_bit);
    } else if (s->pending_ccw > 0 &&
               (int32_t)(now - s->ccw_ready_at) >= 0) {
        s->pending_ccw--;
        s->ccw_off_at   = now + ENC_PULSE_MS;
        s->ccw_ready_at = s->ccw_off_at + ENC_GAP_MS;
        bits |= (1UL << d->ccw_bit);
    }

    /* --- Encoder push button with debounce --- */
    uint8_t sw = (gpio_get(d->sw.port, d->sw.pin) == 0) ? 1 : 0;

    if (sw != s->sw_raw) {
        s->sw_raw = sw;
        s->sw_debounce_ts = now;
    } else if (sw != s->sw_stable) {
        if ((now - s->sw_debounce_ts) >= DEBOUNCE_MS)
            s->sw_stable = sw;
    }

    if (s->sw_stable)
        bits |= (1UL << d->push_bit);

    return bits;
}

static uint32_t scan_encoders(void)
{
    uint32_t now = millis();
    uint32_t bits = 0;
    for (int i = 0; i < NUM_ENCODERS; i++)
        bits |= scan_encoder_one(&variant_encoder_defs[i], &enc_state[i], now);
    return bits;
}

/* Mask of all encoder rotation bits (CW + CCW for every encoder),
 * used to flash LED2 on any knob movement. */
static uint32_t encoder_rotation_mask(void)
{
    uint32_t m = 0;
    for (int i = 0; i < NUM_ENCODERS; i++) {
        m |= (1UL << variant_encoder_defs[i].cw_bit);
        m |= (1UL << variant_encoder_defs[i].ccw_bit);
    }
    return m;
}

/* ============================================================
 *  LED Control
 * ============================================================ */

static void led1_set(int on)
{
    if (on) gpio_set(LED1_PORT, LED1_PIN);
    else    gpio_clear(LED1_PORT, LED1_PIN);
}

static void led2_set(int on)
{
    if (on) gpio_set(LED2_PORT, LED2_PIN);
    else    gpio_clear(LED2_PORT, LED2_PIN);
}

/* ============================================================
 *  USB Descriptors — 32-button joystick
 * ============================================================ */

static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,   /* Usage Page (Generic Desktop)    */
    0x09, 0x04,   /* Usage (Joystick)                */
    0xA1, 0x01,   /* Collection (Application)        */
    0x05, 0x09,   /*   Usage Page (Buttons)          */
    0x19, 0x01,   /*   Usage Minimum (1)             */
    0x29, HID_NUM_BUTTONS,  /*   Usage Maximum (32)  */
    0x15, 0x00,   /*   Logical Minimum (0)           */
    0x25, 0x01,   /*   Logical Maximum (1)           */
    0x75, 0x01,   /*   Report Size (1 bit)           */
    0x95, HID_NUM_BUTTONS,  /*   Report Count (32)   */
    0x81, 0x02,   /*   Input (Data, Var, Abs)        */
    0xC0          /* End Collection                  */
};

static const struct {
    struct usb_hid_descriptor hid_descriptor;
    struct {
        uint8_t bReportDescriptorType;
        uint16_t wDescriptorLength;
    } __attribute__((packed)) hid_report;
} __attribute__((packed)) hid_function = {
    .hid_descriptor = {
        .bLength = sizeof(hid_function),
        .bDescriptorType = USB_DT_HID,
        .bcdHID = 0x0111,
        .bCountryCode = 0,
        .bNumDescriptors = 1,
    },
    .hid_report = {
        .bReportDescriptorType = USB_DT_REPORT,
        .wDescriptorLength = sizeof(hid_report_descriptor),
    },
};

static const struct usb_endpoint_descriptor hid_endpoint = {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = 0x81,
    .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize = 8,
    .bInterval = 1,
};

static const struct usb_interface_descriptor hid_iface = {
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_HID,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface = 0,
    .endpoint = &hid_endpoint,
    .extra = &hid_function,
    .extralen = sizeof(hid_function),
};

static const struct usb_interface ifaces[] = {{
    .num_altsetting = 1,
    .altsetting = &hid_iface,
}};

static const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0x80,
    .bMaxPower = 50,
    .interface = ifaces,
};

static const struct usb_device_descriptor dev_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x1209,
    .idProduct = DEVICE_PID,
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

/* USB serial — filled at boot from the MCU's 96-bit unique ID so
 * every unit enumerates with a distinct 12-hex-digit serial number.
 * Windows keys HID settings per serial, so distinct serials let
 * users plug in multiple boxes without config collisions. */
static char serial_str[13];

static const char *usb_strings[] = {
    "DIY",
    DEVICE_PRODUCT_STR,
    serial_str,
};

/* ============================================================
 *  USB HID Class Callbacks
 * ============================================================ */

static usbd_device *usbd_dev;
static uint8_t usb_control_buffer[128];
static uint8_t usb_configured = 0;

static enum usbd_request_return_codes hid_control_request(
    usbd_device *dev,
    struct usb_setup_data *req,
    uint8_t **buf, uint16_t *len,
    usbd_control_complete_callback *complete)
{
    (void)dev;
    (void)complete;

    if ((req->bmRequestType != 0x81) ||
        (req->bRequest != USB_REQ_GET_DESCRIPTOR) ||
        (req->wValue != 0x2200))
        return USBD_REQ_NOTSUPP;

    *buf = (uint8_t *)hid_report_descriptor;
    *len = sizeof(hid_report_descriptor);

    return USBD_REQ_HANDLED;
}

static void hid_set_config(usbd_device *dev, uint16_t wValue)
{
    (void)wValue;

    usbd_ep_setup(dev, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);

    usbd_register_control_callback(
        dev,
        USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        hid_control_request);

    usb_configured = 1;
}

/* ============================================================
 *  GPIO Setup
 * ============================================================ */

static void gpio_setup_input_pullup(uint32_t port, uint16_t pin)
{
    gpio_set_mode(port, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, pin);
    gpio_set(port, pin);  /* ODR=1 selects pull-UP */
}

static void gpio_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_AFIO);

    /* Disable JTAG, keep SWD — frees PA15, PB3, PB4 for GPIO use.
     * Without this, encoder 5 (PA15/PB3/PB4) stays locked in JTAG
     * mode and the knob won't register. */
    gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON, 0);

    /* Onboard LED on PB2 (active high, WeAct Blue Pill Plus) */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GPIO2);
    gpio_clear(GPIOB, GPIO2);  /* LED off */

    /* Button inputs with pull-up */
    for (int i = 0; i < NUM_BUTTONS; i++)
        gpio_setup_input_pullup(variant_button_pins[i].port, variant_button_pins[i].pin);

    /* Encoder A, B, and push switch with pull-up — for every encoder */
    for (int i = 0; i < NUM_ENCODERS; i++) {
        const encoder_def_t *d = &variant_encoder_defs[i];
        gpio_setup_input_pullup(d->a.port,  d->a.pin);
        gpio_setup_input_pullup(d->b.port,  d->b.pin);
        gpio_setup_input_pullup(d->sw.port, d->sw.pin);
    }

    /* Status LEDs — push-pull, active high */
    gpio_set_mode(LED1_PORT, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, LED1_PIN);
    gpio_set_mode(LED2_PORT, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, LED2_PIN);
    gpio_clear(LED1_PORT, LED1_PIN);
    gpio_clear(LED2_PORT, LED2_PIN);
}

/* ============================================================
 *  USB D+ Force-Reset (Blue Pill enumeration fix)
 *  Not needed on WeAct Blue Pill Plus (correct 1.5kΩ pull-up).
 *  Uncomment if using original Blue Pill with wrong resistor.
 * ============================================================ */

/*
static void usb_force_reset(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);

    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
    gpio_clear(GPIOA, GPIO12);

    for (volatile int i = 0; i < 800000; i++)
        __asm__("nop");

    gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT, GPIO12);

    for (volatile int i = 0; i < 800000; i++)
        __asm__("nop");
}
*/

/* ============================================================
 *  Main
 * ============================================================ */

int main(void)
{
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

    gpio_init();
    /* usb_force_reset(); */  /* Not needed on WeAct Blue Pill Plus */

    /* debounce_ts/raw_state/stable_state are file-scope statics,
     * so they're already zero-initialised by the C startup code. */

    systick_init();
    encoder_init();

    desig_get_unique_id_as_dfu(serial_str);

    usbd_dev = usbd_init(&st_usbfs_v1_usb_driver,
                         &dev_descr, &config,
                         usb_strings, 3,
                         usb_control_buffer,
                         sizeof(usb_control_buffer));

    usbd_register_set_config_callback(usbd_dev, hid_set_config);

    /* Independent watchdog — auto-reset the MCU if the main loop
     * wedges (e.g. USB stack gets stuck). Fed every iteration,
     * so the period only needs to cover worst-case main-loop time. */
    iwdg_set_period_ms(IWDG_PERIOD_MS);
    iwdg_start();

    const uint32_t rot_mask = encoder_rotation_mask();

    uint32_t last_report = 0;
    uint32_t last_led    = 0;
    uint32_t prev_all    = 0xFFFFFFFF;  /* Force first report */
    uint32_t led2_off_at = 0;

    while (1) {
        iwdg_reset();
        usbd_poll(usbd_dev);

        uint32_t now = millis();

        if ((now - last_report) >= 1) {
            last_report = now;

            /* Merge buttons + all encoders into one 32-bit field */
            uint32_t all = scan_buttons() | scan_encoders();

            if (all != prev_all) {
                uint8_t report[HID_REPORT_BYTES] = {
                    (uint8_t)( all        & 0xFF),
                    (uint8_t)((all >>  8) & 0xFF),
                    (uint8_t)((all >> 16) & 0xFF),
                    (uint8_t)((all >> 24) & 0xFF),
                };
                usbd_ep_write_packet(usbd_dev, 0x81,
                                     report, sizeof(report));
                prev_all = all;

                /* Flash LED2 on any encoder rotation */
                if (all & rot_mask) {
                    led2_set(1);
                    led2_off_at = now + 30;
                }
            }
        }

        /* Turn off LED2 after brief flash */
        if (led2_off_at && (int32_t)(millis() - led2_off_at) >= 0) {
            led2_set(0);
            led2_off_at = 0;
        }

        /* LED1 = USB configured indicator */
        led1_set(usb_configured);

        /* Heartbeat blink on PB2 */
        if ((now - last_led) >= LED_BLINK_MS) {
            last_led = now;
            gpio_toggle(GPIOB, GPIO2);
        }
    }
}
