#ifndef CONTROL_CORE_H
#define CONTROL_CORE_H
#include <stdint.h>
typedef struct {
  uint8_t fail_count, recover_count; /* zero = not commissioned */
  uint8_t failures[2], successes[2], valid[2];
  uint8_t source; /* 0=S1, 1=S2, 2=no valid selected source */
  uint8_t manual, requested, logical, fault, sampled, auto_requested;
  uint16_t heater_min_on_seconds, fan_min_on_seconds;
  uint32_t sample_at, manual_at, timeout_ms;
  uint32_t changed_at[4];
} ControlCore;
void control_init(ControlCore *c, uint32_t now);
void control_sample(ControlCore *c, unsigned valid1, unsigned valid2, uint32_t now);
void control_observe(ControlCore *c, unsigned valid1, unsigned valid2, uint32_t now);
void control_tick(ControlCore *c, uint32_t now);
void control_mode(ControlCore *c, unsigned manual, uint32_t now);
int control_relay(ControlCore *c, unsigned relay, unsigned on, uint32_t now);
#endif
