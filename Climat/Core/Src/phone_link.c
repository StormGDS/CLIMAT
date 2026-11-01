#include "phone_link.h"
#include <string.h>
uint16_t phone_crc16(const uint8_t *p, unsigned n)
{
  uint16_t crc=0xffff;
  while(n--) {
    crc^=(uint16_t)*p++<<8;
    for(unsigned i=0;i<8;++i) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
  }
  return crc;
}
unsigned phone_pack(uint8_t op, uint16_t seq, const uint8_t *p, unsigned n, uint8_t wire[PHONE_WIRE_MAX])
{
  if(n>PHONE_PAYLOAD_MAX) return 0;
  uint8_t raw[PHONE_RAW_MAX];raw[0]=PHONE_PROTOCOL_VERSION;raw[1]=op;raw[2]=(uint8_t)seq;
  raw[3]=(uint8_t)(seq>>8);raw[4]=(uint8_t)n;
  if(n) memcpy(raw+5,p,n);
  uint16_t crc=phone_crc16(raw,n+5);raw[n+5]=(uint8_t)crc;raw[n+6]=(uint8_t)(crc>>8);
  /* COBS: both leading and trailing zero delimiters permit recovery from
   * arbitrary modem text preceding a frame. Maximum block is <254 bytes. */
  wire[0]=0;unsigned code_at=1, at=2;uint8_t code=1;
  for(unsigned i=0;i<n+7;++i) {
    if(!raw[i]) {wire[code_at]=code;code_at=at++;code=1;}
    else {wire[at++]=raw[i];++code;}
  }
  wire[code_at]=code;wire[at++]=0;return at;
}
unsigned phone_unstuff(const uint8_t *p, unsigned n, uint8_t *out)
{
  unsigned at=0,used=0;
  while(at<n) {
    unsigned code=p[at++];
    if(!code || code-1>n-at || used+code-1>PHONE_RAW_MAX) return 0;
    for(unsigned i=1;i<code;++i) {if(!p[at]) return 0;out[used++]=p[at++];}
    if(code!=255 && at<n) {if(used==PHONE_RAW_MAX) return 0;out[used++]=0;}
  }
  return used;
}
void phone_init(PhoneLink *l, PhoneDispatch dispatch, PhoneSend send, void *context)
{memset(l,0,sizeof(*l));l->dispatch=dispatch;l->send=send;l->context=context;}
void phone_loss(PhoneLink *l) {l->used=0;l->discard=1;++l->rejected;}
void phone_tick(PhoneLink *l, uint32_t now)
{if(l->used && (uint32_t)(now-l->last_at)>=500) phone_loss(l);}
static void frame(PhoneLink *l, uint32_t now)
{
  uint8_t raw[PHONE_RAW_MAX], payload[PHONE_PAYLOAD_MAX];
  unsigned n=phone_unstuff(l->incoming,l->used,raw);
  if(n<7 || raw[4]>PHONE_PAYLOAD_MAX || n!=(unsigned)raw[4]+7 ||
     phone_crc16(raw,n-2)!=(uint16_t)(raw[n-2]|((uint16_t)raw[n-1]<<8)) ||
     raw[1]&0x80) {++l->rejected;return;}
  uint16_t seq=(uint16_t)(raw[2]|((uint16_t)raw[3]<<8));
  if(l->cached && raw[2]==l->previous[2] && raw[3]==l->previous[3]) {
    if(n==l->previous_size && !memcmp(raw,l->previous,n)) {
      ++l->duplicates;l->send(l->context,l->reply,l->reply_size);return;
    }
    uint8_t conflict[PHONE_WIRE_MAX];payload[0]=PHONE_SEQUENCE;
    unsigned size=phone_pack(raw[1]|0x80,seq,payload,1,conflict);
    l->send(l->context,conflict,size);return;
  }
  unsigned length;
  if(raw[0]!=PHONE_PROTOCOL_VERSION) {payload[0]=PHONE_VERSION;length=1;}
  else length=l->dispatch(l->context,raw[1],raw+5,raw[4],payload,now);
  if(!length || length>PHONE_PAYLOAD_MAX) {payload[0]=PHONE_BAD_PAYLOAD;length=1;}
  memcpy(l->previous,raw,n);l->previous_size=(uint8_t)n;l->cached=1;
  l->reply_size=(uint8_t)phone_pack(raw[1]|0x80,seq,payload,length,l->reply);
  ++l->accepted;l->send(l->context,l->reply,l->reply_size);
}
void phone_feed(PhoneLink *l, uint8_t byte, uint32_t now)
{
  phone_tick(l,now);l->last_at=now;
  if(!byte) {
    if(l->used && !l->discard) frame(l,now);
    l->used=0;l->discard=0;
  } else if(!l->discard) {
    if(l->used==sizeof(l->incoming)) phone_loss(l);
    else l->incoming[l->used++]=byte;
  }
}
