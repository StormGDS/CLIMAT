#ifndef SERVICE_CONSOLE_H
#define SERVICE_CONSOLE_H
#include "temperature_probe.h"
#include "control_core.h"
#include "settings.h"
void service_init(TemperatureProbe *probe, void (*report)(uint32_t), void (*output)(const char *), SettingsBackend storage);
void service_poll(uint32_t now);
void service_sample(uint32_t now);
void service_rx_byte(uint8_t byte); /* ISR producer */
void service_rx_error(void); /* ISR producer */
unsigned service_stream_enabled(void);
void service_set_bt_handler(int (*handler)(const char *));
/* Optional board driver: apply arbiter mask and return GPIO output mask.
 * NULL keeps physical outputs locked (the default until a board driver is registered). */
void service_set_relay_driver(uint8_t (*driver)(uint8_t mask));
void service_phone_init(void (*send)(const uint8_t *, unsigned));
void service_phone_byte(uint8_t byte); /* ISR producer */
void service_phone_loss(void); /* ISR producer */
void service_phone_poll(uint32_t now);
void service_device_name(uint8_t name[3]);
void service_set_phone_filter(int (*filter)(uint8_t,uint32_t));
#endif
