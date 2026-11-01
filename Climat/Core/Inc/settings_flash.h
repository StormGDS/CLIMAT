#ifndef SETTINGS_FLASH_H
#define SETTINGS_FLASH_H
#include "settings.h"
int settings_flash_read(uint8_t data[SETTINGS_SIZE]);
int settings_flash_write(const uint8_t data[SETTINGS_SIZE]);
#endif
