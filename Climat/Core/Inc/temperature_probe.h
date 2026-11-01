#ifndef TEMPERATURE_PROBE_H
#define TEMPERATURE_PROBE_H
#include <stdint.h>
#include <stddef.h>
#include "thermal_timing.h"

/* Portable probe; GPIO timing and console belong to the board adapter. */
typedef struct {
  int (*reset)(void); /* 1 presence, 0 absent, -1 bus stuck low */
  void (*write_bit)(unsigned bit);
  unsigned (*read_bit)(void);
  uint32_t (*now_ms)(void);
} ProbeBus;
typedef struct {
  uint8_t rom[8];
  uint8_t assigned;
  uint8_t valid;
  int16_t raw16;
  uint32_t errors;
  const char *status;
  int16_t history[TREND_SAMPLES];
  uint32_t time[TREND_SAMPLES];
  uint8_t count, head;
  int32_t rate_milli_per_min;
  uint8_t trend_valid;
} ProbeSensor;
typedef struct {
  ProbeBus bus;
  ProbeSensor sensor[2];
  uint8_t discovered[2][8], discovered_count;
  uint32_t cycle_at, convert_at, scan_at;
  uint8_t converting, started;
  uint8_t scan_limited;
  const char *scan_status;
} TemperatureProbe;
uint8_t probe_crc8(const uint8_t *data, size_t length);
int probe_decode(const uint8_t scratchpad[9], int16_t *raw16);
int probe_bindings_valid(const uint8_t rom[2][8]);
int probe_set_bindings(TemperatureProbe *p, const uint8_t rom[2][8]);
void probe_init(TemperatureProbe *p, ProbeBus bus);
/* Returns 1 when a complete result (including errors) is ready to print. */
int probe_poll(TemperatureProbe *p, uint32_t now);
uint32_t probe_trend_selftest(void);
#endif
