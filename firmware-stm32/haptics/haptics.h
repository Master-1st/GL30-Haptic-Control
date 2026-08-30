#ifndef GL30_HAPTICS_H_
#define GL30_HAPTICS_H_

#include <stdbool.h>

#include "foc.h"

void gl30_haptic_tick_2k(gl30_foc_state_t *state, bool torque_allowed);

#endif
