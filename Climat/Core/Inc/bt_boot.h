#ifndef BT_BOOT_H
#define BT_BOOT_H
#include <stdint.h>
enum { BT_POWER_WAIT, BT_PROBE, BT_VERSION, BT_ROLE_READ, BT_ROLE_SET, BT_ROLE_VERIFY,
       BT_NAME_READ, BT_NAME_SET, BT_NAME_VERIFY, BT_UUID_READ, BT_UUID_SET, BT_UUID_VERIFY,
       BT_CHAR_READ, BT_CHAR_SET, BT_CHAR_VERIFY, BT_WRITE_WAIT, BT_WRITE_PROBE, BT_RESET_WAIT, BT_UART_WAIT, BT_READY, BT_NO_AT, BT_UNSUPPORTED,
       BT_FAILED, BT_LINK_ACTIVE };
typedef struct {
  char response[64],name[11];
  uint8_t state,used,present,writes,retried,reset_done,verify_next,phase;
  uint32_t started,at,last_rx,baud;
  uint8_t baud_index;
  int (*set_baud)(void *,uint32_t);
  int (*send)(void *,const char *);void *context;
} BtBoot;
void bt_boot_init(BtBoot *b, const uint8_t name[3], uint32_t now,
                  int (*send)(void *,const char *), void *context);
/* Optional MCU UART speed selector; module baud is never rewritten. */
void bt_boot_set_uart(BtBoot *b, int (*set_baud)(void *,uint32_t));
void bt_boot_poll(BtBoot *b, uint32_t now);
/* 1=consumed as startup response; 0=pass byte into phone protocol. */
int bt_boot_byte(BtBoot *b, uint8_t byte, uint32_t now);
unsigned bt_boot_busy(const BtBoot *b);
const char *bt_boot_status(const BtBoot *b);
uint32_t bt_boot_selftest(void);
#endif
