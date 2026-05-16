/*
 * variants/default.h — 7 buttons + 6 rotary encoders.
 *
 * Each encoder occupies a contiguous 3-pin header block. See
 * pinout-default.svg for the physical layout.
 *
 * Build with: make            (or  make VARIANT=default)
 */

#ifndef VARIANT_DEFAULT_H
#define VARIANT_DEFAULT_H

#include "../button_box.h"

#define DEVICE_PID          0x0001
#define DEVICE_PRODUCT_STR  "STM32 Button Box"

#define NUM_BUTTONS   7
#define NUM_ENCODERS  6

static const pin_t variant_button_pins[NUM_BUTTONS] = {
    { GPIOB, GPIO0  },   /* Btn 1 */
    { GPIOB, GPIO1  },   /* Btn 2 */
    { GPIOB, GPIO8  },   /* Btn 3 */
    { GPIOB, GPIO9  },   /* Btn 4 */
    { GPIOB, GPIO10 },   /* Btn 5 */
    { GPIOB, GPIO11 },   /* Btn 6 */
    { GPIOB, GPIO15 },   /* Btn 7 */
};

static const encoder_def_t variant_encoder_defs[NUM_ENCODERS] = {
    /*       A                    B                    Push          CW CCW Push */
    { { GPIOA, GPIO8  }, { GPIOA, GPIO9  }, { GPIOA, GPIO10 },  7,  8,  9 },
    { { GPIOB, GPIO12 }, { GPIOB, GPIO13 }, { GPIOB, GPIO14 }, 10, 11, 12 },
    { { GPIOA, GPIO5  }, { GPIOA, GPIO6  }, { GPIOA, GPIO7  }, 13, 14, 15 },
    { { GPIOA, GPIO0  }, { GPIOA, GPIO1  }, { GPIOA, GPIO2  }, 16, 17, 18 },
    { { GPIOA, GPIO15 }, { GPIOB, GPIO3  }, { GPIOB, GPIO4  }, 19, 20, 21 },
    { { GPIOB, GPIO5  }, { GPIOB, GPIO6  }, { GPIOB, GPIO7  }, 22, 23, 24 },
};

#endif
