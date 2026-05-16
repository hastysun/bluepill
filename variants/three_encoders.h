/*
 * variants/three_encoders.h — 16 buttons + 3 rotary encoders.
 *
 * Three encoders spread across both sides of the header (PA8-10,
 * PB12-14, PA5-7); all remaining input-capable pins are buttons.
 *
 * Build with: make VARIANT=three_encoders
 */

#ifndef VARIANT_THREE_ENCODERS_H
#define VARIANT_THREE_ENCODERS_H

#include "../button_box.h"

#define DEVICE_PID          0x0003
#define DEVICE_PRODUCT_STR  "STM32 Mixed Box"

#define NUM_BUTTONS   16
#define NUM_ENCODERS  3

static const pin_t variant_button_pins[NUM_BUTTONS] = {
    { GPIOB, GPIO0  },   /* Btn  1 */
    { GPIOB, GPIO1  },   /* Btn  2 */
    { GPIOB, GPIO3  },   /* Btn  3 */
    { GPIOB, GPIO4  },   /* Btn  4 */
    { GPIOB, GPIO5  },   /* Btn  5 */
    { GPIOB, GPIO6  },   /* Btn  6 */
    { GPIOB, GPIO7  },   /* Btn  7 */
    { GPIOB, GPIO8  },   /* Btn  8 */
    { GPIOB, GPIO9  },   /* Btn  9 */
    { GPIOB, GPIO10 },   /* Btn 10 */
    { GPIOB, GPIO11 },   /* Btn 11 */
    { GPIOB, GPIO15 },   /* Btn 12 */
    { GPIOA, GPIO0  },   /* Btn 13 */
    { GPIOA, GPIO1  },   /* Btn 14 */
    { GPIOA, GPIO2  },   /* Btn 15 */
    { GPIOA, GPIO15 },   /* Btn 16 */
};

static const encoder_def_t variant_encoder_defs[NUM_ENCODERS] = {
    /*       A                    B                    Push          CW CCW Push */
    { { GPIOA, GPIO8  }, { GPIOA, GPIO9  }, { GPIOA, GPIO10 }, 16, 17, 18 },
    { { GPIOB, GPIO12 }, { GPIOB, GPIO13 }, { GPIOB, GPIO14 }, 19, 20, 21 },
    { { GPIOA, GPIO5  }, { GPIOA, GPIO6  }, { GPIOA, GPIO7  }, 22, 23, 24 },
};

#endif
