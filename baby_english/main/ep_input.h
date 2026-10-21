#pragma once
#include "ep_core.h"
typedef struct {
  int stable, candidate;
  uint32_t changed, pressed;
  bool armed, consumed, fired;
} ep_input_t;
void ep_input_init(ep_input_t *input, uint32_t now);
/* ADC ladder: <150mV UP, <447 DOWN, <1900 OK, otherwise released.
 * Negative values are invalid samples; they never produce an action.
 * A tap reports UP/DOWN/OK when the key comes back up; holding it for a second
 * reports EP_LONG once and swallows that release.
 */
ep_input_event_t ep_input_tick(ep_input_t *input, int mv, uint32_t now, bool dimmed);
