#ifndef SETTINGS_H
#define SETTINGS_H
#include "auto_control.h"
#include <stddef.h>
#define SETTINGS_SIZE 72U
#define SETTINGS_VERSION 5U
typedef struct {
  AutoConfig automatic;
  uint8_t sensor_rom[2][8];
  uint16_t manual_seconds;
  uint16_t heater_min_on_seconds, fan_min_on_seconds;
  uint8_t fail_count, recover_count, alternate;
  uint8_t device_name[3]; /* Opaque 3-byte short name; 000000 = unassigned. */
} Settings;
typedef enum { SETTINGS_OK, SETTINGS_EMPTY, SETTINGS_INVALID, SETTINGS_IO_ERROR } SettingsResult;
typedef struct {
  int (*read)(uint8_t data[SETTINGS_SIZE]);
  int (*write)(const uint8_t data[SETTINGS_SIZE]);
} SettingsBackend;
void settings_defaults(Settings *s);
uint32_t settings_crc32(const uint8_t *data, size_t length);
int settings_encode(const Settings *s, uint8_t data[SETTINGS_SIZE]);
SettingsResult settings_decode(const uint8_t data[SETTINGS_SIZE], Settings *s);
SettingsResult settings_load(SettingsBackend io, Settings *s);
SettingsResult settings_save(SettingsBackend io, const Settings *s, unsigned *unchanged);
uint32_t settings_selftest(void);
#endif
