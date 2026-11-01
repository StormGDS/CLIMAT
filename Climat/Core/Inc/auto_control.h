#ifndef AUTO_CONTROL_H
#define AUTO_CONTROL_H
#include <stdint.h>
#define AUTO_MIN_RAW16 168
/* Temperatures and hysteresis: 1/16 C; positive cooling speed: 0.001 C/min.
 * cooling_milli_per_min=0 disables early K2 until commissioned. */
typedef struct {
  int16_t heat1_on, heat1_hyst, heat2_on, heat2_hyst;
  int16_t cool1_on, cool1_hyst, cool2_on, cool2_hyst;
  uint16_t cooling_milli_per_min;
  uint16_t rising_milli_per_min, rate_hyst_milli_per_min;
} AutoConfig;
typedef struct {
  AutoConfig config;
  uint8_t configured, heat, cool, lead, cycle, source, tracking, rate_ready, alternate;
  uint8_t heat2_temperature, rising_blocked, cooling_early;
  uint32_t source_at;
  int32_t effective_milli;
} AutoControl;
void auto_config_write(const AutoConfig *config, uint8_t bytes[18]);
void auto_config_read(AutoConfig *config, const uint8_t bytes[18]);
int auto_configure(AutoControl *a, const AutoConfig *config);
void auto_reset(AutoControl *a);
uint8_t auto_sample(AutoControl *a, unsigned source, unsigned valid,
                   int16_t raw16, unsigned trend_valid, int32_t rate_milli,
                   uint8_t actual, uint32_t now);
uint32_t auto_selftest(void);
#endif
