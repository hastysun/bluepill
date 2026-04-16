/**
 * main.c — STM32F103 USB HID Button Box (libopencm3)
 *
 * 24-button USB joystick for the Blue Pill:
 *   - 16 physical buttons/toggle switches (buttons 1–16)
 *   - 1 rotary encoder as CW/CCW button pulses (buttons 17–18)
 *   - 1 encoder push button (button 19)
 *   - Buttons 20–24 reserved for future use
 *   - 2 status LEDs
 *
 * Encoder pulses are ideal for DCS World — each detent click sends
 * a brief button press that DCS can bind to radio tuning, altimeter
 * setting, etc.
 *
 * Buttons are active-low with internal pull-ups: wire each to GND.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/st_usbfs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <string.h>

/* ============================================================
 *  Configuration
 * ============================================================ */

#define NUM_BUTTONS       19       /* Physical button/switch inputs    */
#define DEBOUNCE_MS       5        /* Button debounce time             */
#define ENC_PULSE_MS      50       /* How long encoder "press" lasts   */
#define LED_BLINK_MS      500      /* Onboard LED half-period          */

/* HID button indices (0-based bit positions) */
#define BTN_ENC_CW        19       /* Button 20: encoder clockwise     */
#define BTN_ENC_CCW       20       /* Button 21: encoder counter-CW    */
#define BTN_ENC_PUSH      21       /* Button 22: encoder push button   */
#define HID_NUM_BUTTONS   24       /* Total HID buttons (3 bytes)      */

/* Ignition switch — PB12/PB13/PB14 are buttons 12/13/14 in the
 * raw scan (bits 11/12/13). We mask them out and decode the
 * combination into one button per position on bits 19/20/21.  */
#define IGN_RAW_MASK      ((1UL << 11) | (1UL << 12) | (1UL << 13))
#define BTN_IGN_ACC       11       /* Button 12: ACC position          */
#define BTN_IGN_ON        12       /* Button 13: ON position           */
#define BTN_IGN_START     13       /* Button 14: START position        */

/* ============================================================
 *  Pin Map
 * ============================================================
 *
 *  --- 19 Buttons (active-low, internal pull-up) ---
 *  Btn 1  = PB0    Btn 10 = PB10
 *  Btn 2  = PB1    Btn 11 = PB11
 *  Btn 3  = PB3    Btn 12 = PB12 (IGN ACC)
 *  Btn 4  = PB4    Btn 13 = PB13 (IGN ON)
 *  Btn 5  = PB5    Btn 14 = PB14 (IGN START)
 *  Btn 6  = PB6    Btn 15 = PB15
 *  Btn 7  = PB7    Btn 16 = PA8
 *  Btn 8  = PB8    Btn 17 = PA0
 *  Btn 9  = PB9    Btn 18 = PA1
 *                   Btn 19 = PA2
 *
 *  --- Rotary Encoder (active-low, internal pull-up) ---
 *  Encoder A    = PA9
 *  Encoder B    = PA10
 *  Encoder Push = PA15
 *
 *  --- Status LEDs (active-high, push-pull) ---
 *  LED 1  = PA3   ("USB connected" indicator)
 *  LED 2  = PA4   ("encoder activity" flash)
 *
 *  --- Reserved ---
 *  PA11/PA12 = USB D-/D+
 *  PB2       = Onboard LED (active-high, heartbeat blink)
 *  PC13      = Unused (was LED on original Blue Pill)
 */

typedef struct {
    uint32_t port;
    uint16_t pin;
} pin_t;

static const pin_t button_pins[NUM_BUTTONS] = {
    { GPIOB, GPIO0  },   /* 1  */
    { GPIOB, GPIO1  },   /* 2  */
    { GPIOB, GPIO3  },   /* 3  */
    { GPIOB, GPIO4  },   /* 4  */
    { GPIOB, GPIO5  },   /* 5  */
    { GPIOB, GPIO6  },   /* 6  */
    { GPIOB, GPIO7  },   /* 7  */
    { GPIOB, GPIO8  },   /* 8  */
    { GPIOB, GPIO9  },   /* 9  */
    { GPIOB, GPIO10 },   /* 10 */
    { GPIOB, GPIO11 },   /* 11 */
    { GPIOB, GPIO12 },   /* 12 */
    { GPIOB, GPIO13 },   /* 13 */
    { GPIOB, GPIO14 },   /* 14 */
    { GPIOB, GPIO15 },   /* 15 */
    { GPIOA, GPIO8  },   /* 16 */
    { GPIOA, GPIO0  },   /* 17 */
    { GPIOA, GPIO1  },   /* 18 */
    { GPIOA, GPIO2  },   /* 19 */
};

/* Encoder pins */
#define ENC_A_PORT   GPIOA
#define ENC_A_PIN    GPIO9
#define ENC_B_PORT   GPIOA
#define ENC_B_PIN    GPIO10
#define ENC_SW_PORT  GPIOA
#define ENC_SW_PIN   GPIO15

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
 *  Button Debouncer (buttons 1–16)
 * ============================================================ */

static uint32_t debounce_ts[NUM_BUTTONS];
static uint8_t  raw_state[NUM_BUTTONS];
static uint8_t  stable_state[NUM_BUTTONS];

static uint32_t scan_buttons(void)
{
    uint32_t now = millis();
    uint32_t result = 0;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        uint8_t current = (gpio_get(button_pins[i].port,
                                     button_pins[i].pin) == 0) ? 1 : 0;

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
 *  Rotary Encoder — Quadrature Decoder
 * ============================================================
 *
 *  Uses a 2-bit state machine on the A/B signals. Each full
 *  detent click produces one CW or CCW event. The event is
 *  reported as a brief button pulse lasting ENC_PULSE_MS so
 *  DCS (and other sims) register it as a button press.
 *
 *  Wiring: encoder common to GND, A→PA9, B→PA10, push→PA15.
 *  Most encoders with detents produce one full quadrature
 *  cycle per detent (4 state transitions, 1 count).
 */

static uint8_t  enc_last_state;
static uint32_t enc_cw_until;     /* millis() when CW pulse expires  */
static uint32_t enc_ccw_until;    /* millis() when CCW pulse expires  */

/* Encoder push button debounce */
static uint32_t enc_sw_debounce_ts;
static uint8_t  enc_sw_raw;
static uint8_t  enc_sw_stable;

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
    uint8_t a = gpio_get(ENC_A_PORT, ENC_A_PIN) ? 1 : 0;
    uint8_t b = gpio_get(ENC_B_PORT, ENC_B_PIN) ? 1 : 0;
    enc_last_state = (a << 1) | b;
    enc_cw_until = 0;
    enc_ccw_until = 0;
    enc_sw_debounce_ts = 0;
    enc_sw_raw = 0;
    enc_sw_stable = 0;
}

static uint32_t scan_encoder(void)
{
    uint32_t now = millis();
    uint32_t bits = 0;

    /* --- Quadrature decode --- */
    uint8_t a = gpio_get(ENC_A_PORT, ENC_A_PIN) ? 1 : 0;
    uint8_t b = gpio_get(ENC_B_PORT, ENC_B_PIN) ? 1 : 0;
    uint8_t curr_state = (a << 1) | b;

    if (curr_state != enc_last_state) {
        int8_t dir = enc_table[(enc_last_state << 2) | curr_state];
        if (dir == +1)
            enc_cw_until = now + ENC_PULSE_MS;
        else if (dir == -1)
            enc_ccw_until = now + ENC_PULSE_MS;
        enc_last_state = curr_state;
    }

    /* CW pulse active? */
    if (enc_cw_until && (int32_t)(now - enc_cw_until) < 0)
        bits |= (1UL << BTN_ENC_CW);
    else
        enc_cw_until = 0;

    /* CCW pulse active? */
    if (enc_ccw_until && (int32_t)(now - enc_ccw_until) < 0)
        bits |= (1UL << BTN_ENC_CCW);
    else
        enc_ccw_until = 0;

    /* --- Encoder push button with debounce --- */
    uint8_t sw = (gpio_get(ENC_SW_PORT, ENC_SW_PIN) == 0) ? 1 : 0;

    if (sw != enc_sw_raw) {
        enc_sw_raw = sw;
        enc_sw_debounce_ts = now;
    } else if (sw != enc_sw_stable) {
        if ((now - enc_sw_debounce_ts) >= DEBOUNCE_MS)
            enc_sw_stable = sw;
    }

    if (enc_sw_stable)
        bits |= (1UL << BTN_ENC_PUSH);

    return bits;
}

/* ============================================================
 *  Ignition Switch Decoder
 * ============================================================
 *
 *  Reads the raw debounced state of PB12 (ACC), PB13 (IGN),
 *  PB14 (START) and decodes the combination into a single
 *  button per position:
 *
 *    ACC only         → Button 12 (ACC)
 *    ACC + IGN        → Button 13 (ON)
 *    ACC + IGN + START→ Button 13 + 14 (ON + START)
 *    None             → nothing  (OFF)
 */

static uint32_t scan_ignition(uint32_t raw_buttons)
{
    uint8_t acc   = (raw_buttons & (1UL << 11)) ? 1 : 0;
    uint8_t ign   = (raw_buttons & (1UL << 12)) ? 1 : 0;
    uint8_t start = (raw_buttons & (1UL << 13)) ? 1 : 0;

    uint32_t bits = 0;

    if (start)
        bits = (1UL << BTN_IGN_START) | (1UL << BTN_IGN_ON);
    else if (acc && ign)
        bits = (1UL << BTN_IGN_ON);
    else if (acc)
        bits = (1UL << BTN_IGN_ACC);

    return bits;
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
 *  USB Descriptors — 24-button joystick
 * ============================================================ */

static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,   /* Usage Page (Generic Desktop)    */
    0x09, 0x04,   /* Usage (Joystick)                */
    0xA1, 0x01,   /* Collection (Application)        */
    0x05, 0x09,   /*   Usage Page (Buttons)          */
    0x19, 0x01,   /*   Usage Minimum (1)             */
    0x29, 0x18,   /*   Usage Maximum (24)            */
    0x15, 0x00,   /*   Logical Minimum (0)           */
    0x25, 0x01,   /*   Logical Maximum (1)           */
    0x75, 0x01,   /*   Report Size (1 bit)           */
    0x95, 0x18,   /*   Report Count (24)             */
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
    .idProduct = 0x0001,
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const char *usb_strings[] = {
    "DIY",
    "STM32 Button Box",
    "BB-001",
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
     * Without this, PA15 (encoder push) and PB3/PB4 (buttons 3/4)
     * stay locked in JTAG mode and won't work as inputs. */
    gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON, 0);

    /* Onboard LED on PB2 (active high, WeAct Blue Pill Plus) */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GPIO2);
    gpio_clear(GPIOB, GPIO2);  /* LED off */

    /* 19 button inputs with pull-up */
    for (int i = 0; i < NUM_BUTTONS; i++)
        gpio_setup_input_pullup(button_pins[i].port, button_pins[i].pin);

    /* Encoder A, B, and push switch with pull-up */
    gpio_setup_input_pullup(ENC_A_PORT, ENC_A_PIN);
    gpio_setup_input_pullup(ENC_B_PORT, ENC_B_PIN);
    gpio_setup_input_pullup(ENC_SW_PORT, ENC_SW_PIN);

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

    memset(debounce_ts, 0, sizeof(debounce_ts));
    memset(raw_state, 0, sizeof(raw_state));
    memset(stable_state, 0, sizeof(stable_state));

    systick_init();
    encoder_init();

    usbd_dev = usbd_init(&st_usbfs_v1_usb_driver,
                         &dev_descr, &config,
                         usb_strings, 3,
                         usb_control_buffer,
                         sizeof(usb_control_buffer));

    usbd_register_set_config_callback(usbd_dev, hid_set_config);

    uint32_t last_report = 0;
    uint32_t last_led    = 0;
    uint32_t prev_all    = 0xFFFFFFFF;  /* Force first report */
    uint32_t led2_off_at = 0;

    while (1) {
        usbd_poll(usbd_dev);

        uint32_t now = millis();

        if ((now - last_report) >= 1) {
            last_report = now;

            /* Merge all inputs into one 24-bit button field */
            uint32_t raw_btns = scan_buttons();
            uint32_t ign = scan_ignition(raw_btns);
            uint32_t all = (raw_btns & ~IGN_RAW_MASK) | scan_encoder() | ign;

            if (all != prev_all) {
                uint8_t report[3] = {
                    (uint8_t)( all        & 0xFF),
                    (uint8_t)((all >>  8) & 0xFF),
                    (uint8_t)((all >> 16) & 0xFF),
                };
                usbd_ep_write_packet(usbd_dev, 0x81,
                                     report, sizeof(report));
                prev_all = all;

                /* Flash LED2 on any encoder rotation */
                if (all & ((1UL << BTN_ENC_CW) | (1UL << BTN_ENC_CCW))) {
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
