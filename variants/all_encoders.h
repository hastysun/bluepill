/*
 * variants/all_encoders.h — 4 buttons + 7 rotary encoders.
 *
 * Maximum encoder layout: every contiguous 3-pin block on the
 * header is dedicated to an encoder. The four header pins left
 * over (the singletons that don't form a 3-block) become buttons.
 *
 * Distinct PID + product string so Windows/DCS treat this as
 * a different device from the default variant.
 *
 * Build with: make VARIANT=all_encoders
 */

#ifndef VARIANT_ALL_ENCODERS_H
#define VARIANT_ALL_ENCODERS_H

#include "../button_box.h"

#define DEVICE_PID          0x0002
#define DEVICE_PRODUCT_STR  "STM32 Encoder Box"

#define NUM_BUTTONS   4
#define NUM_ENCODERS  7

static const pin_t variant_button_pins[NUM_BUTTONS] = {
    { GPIOB, GPIO0  },   /* Btn 1 */
    { GPIOB, GPIO8  },   /* Btn 2 */
    { GPIOB, GPIO9  },   /* Btn 3 */
    { GPIOB, GPIO15 },   /* Btn 4 */
};

static const encoder_def_t variant_encoder_defs[NUM_ENCODERS] = {
    /*       A                    B                    Push          CW CCW Push */
    { { GPIOA, GPIO8  }, { GPIOA, GPIO9  }, { GPIOA, GPIO10 },  4,  5,  6 },
    { { GPIOB, GPIO12 }, { GPIOB, GPIO13 }, { GPIOB, GPIO14 },  7,  8,  9 },
    { { GPIOA, GPIO5  }, { GPIOA, GPIO6  }, { GPIOA, GPIO7  }, 10, 11, 12 },
    { { GPIOA, GPIO0  }, { GPIOA, GPIO1  }, { GPIOA, GPIO2  }, 13, 14, 15 },
    { { GPIOA, GPIO15 }, { GPIOB, GPIO3  }, { GPIOB, GPIO4  }, 16, 17, 18 },
    { { GPIOB, GPIO5  }, { GPIOB, GPIO6  }, { GPIOB, GPIO7  }, 19, 20, 21 },
    { { GPIOB, GPIO11 }, { GPIOB, GPIO10 }, { GPIOB, GPIO1  }, 22, 23, 24 },
};

#endif
