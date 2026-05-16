/*
 * button_box.h — Shared types for main.c and variant configuration headers.
 *
 * Every variant in variants/*.h defines its pin map using the types
 * declared here, so the build system can swap a single -DVARIANT_CONFIG
 * include without touching main.c.
 */

#ifndef BUTTON_BOX_H
#define BUTTON_BOX_H

#include <libopencm3/stm32/gpio.h>
#include <stdint.h>

typedef struct {
    uint32_t port;
    uint16_t pin;
} pin_t;

typedef struct {
    pin_t   a;        /* Quadrature A input */
    pin_t   b;        /* Quadrature B input */
    pin_t   sw;       /* Push switch input  */
    uint8_t cw_bit;   /* HID bit index for CW pulse  */
    uint8_t ccw_bit;  /* HID bit index for CCW pulse */
    uint8_t push_bit; /* HID bit index for push     */
} encoder_def_t;

#endif
