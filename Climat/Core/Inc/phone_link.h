#ifndef PHONE_LINK_H
#define PHONE_LINK_H
#include <stdint.h>
#include <stddef.h>
#define PHONE_PROTOCOL_VERSION 3U
#define PHONE_PAYLOAD_MAX 64U
#define PHONE_RAW_MAX (PHONE_PAYLOAD_MAX+7U)
#define PHONE_WIRE_MAX (PHONE_RAW_MAX+3U)
enum { PHONE_OK, PHONE_BAD_PAYLOAD, PHONE_BAD_OP, PHONE_BAD_CONFIG,
       PHONE_DENIED, PHONE_STORAGE_ERROR, PHONE_BUSY, PHONE_SEQUENCE, PHONE_VERSION };
typedef unsigned (*PhoneDispatch)(void *context, uint8_t op, const uint8_t *request,
  unsigned length, uint8_t response[PHONE_PAYLOAD_MAX], uint32_t now);
typedef void (*PhoneSend)(void *context, const uint8_t *wire, unsigned length);
typedef struct {
  uint8_t incoming[PHONE_RAW_MAX+1U], previous[PHONE_RAW_MAX], reply[PHONE_WIRE_MAX];
  uint8_t used, discard, cached, previous_size, reply_size;
  uint32_t last_at, accepted, rejected, duplicates;
  PhoneDispatch dispatch;PhoneSend send;void *context;
} PhoneLink;
uint16_t phone_crc16(const uint8_t *bytes, unsigned size);
unsigned phone_pack(uint8_t op, uint16_t sequence, const uint8_t *payload,
                    unsigned length, uint8_t wire[PHONE_WIRE_MAX]);
unsigned phone_unstuff(const uint8_t *encoded, unsigned length, uint8_t *decoded);
void phone_init(PhoneLink *link, PhoneDispatch dispatch, PhoneSend send, void *context);
void phone_loss(PhoneLink *link);
void phone_tick(PhoneLink *link, uint32_t now);
void phone_feed(PhoneLink *link, uint8_t byte, uint32_t now);
uint32_t phone_selftest(void);
#endif
