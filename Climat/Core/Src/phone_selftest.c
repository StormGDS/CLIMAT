#include "phone_link.h"
#include <string.h>
typedef struct {unsigned calls,size;uint8_t reply[PHONE_WIRE_MAX];} Mock;
static unsigned dispatch(void *context, uint8_t op, const uint8_t *p,
                         unsigned n, uint8_t *r, uint32_t now)
{
  (void)op;(void)now;Mock *m=context;++m->calls;r[0]=PHONE_OK;
  if(n>63) n=63;
  if(n) memcpy(r+1,p,n);
  return n+1;
}
static void send(void *context, const uint8_t *p, unsigned n)
{Mock *m=context;m->size=n;memcpy(m->reply,p,n);}
static void feed(PhoneLink *l, const uint8_t *p, unsigned n, uint32_t now)
{while(n--) phone_feed(l,*p++,now);}
uint32_t phone_selftest(void)
{
  uint32_t e=0;PhoneLink l;Mock m={0};
  uint8_t wire[PHONE_WIRE_MAX],raw[PHONE_RAW_MAX],payload[PHONE_PAYLOAD_MAX];
  if(phone_crc16((const uint8_t *)"123456789",9)!=0x29b1) e|=1;
  phone_init(&l,dispatch,send,&m);
  /* Every payload length, including zero bytes and full capacity. */
  for(unsigned n=0;n<=PHONE_PAYLOAD_MAX;++n) {
    for(unsigned i=0;i<n;++i) payload[i]=(uint8_t)(i%3?i:0);
    unsigned size=phone_pack(1,(uint16_t)n,payload,n,wire);
    if(size>PHONE_WIRE_MAX || wire[0] || wire[size-1]) e|=2;
    unsigned decoded=phone_unstuff(wire+1,size-2,raw);
    if(decoded!=n+7 || (n && memcmp(raw+5,payload,n))) e|=4;
    unsigned before=m.calls;feed(&l,wire,size,n);
    if(m.calls!=before+1 || !m.size) e|=8;
  }
  if(phone_pack(1,0,payload,65,wire)) e|=16;
  phone_init(&l,dispatch,send,&m);
  unsigned size=phone_pack(9,100,(const uint8_t *)"\1",1,wire);
  unsigned before=m.calls;feed(&l,wire,size,0);feed(&l,wire,size,1);
  if(m.calls!=before+1 || l.duplicates!=1) e|=32;
  size=phone_pack(9,100,(const uint8_t *)"\0",1,wire);feed(&l,wire,size,2);
  unsigned decoded=phone_unstuff(m.reply+1,m.size-2,raw);
  if(m.calls!=before+1 || decoded<8 || raw[5]!=PHONE_SEQUENCE) e|=64;
  /* Each bit corruption must not reach the application. */
  for(unsigned byte=1;byte<size-1;++byte) for(unsigned bit=0;bit<8;++bit) {
    phone_init(&l,dispatch,send,&m);before=m.calls;
    wire[byte]^=(uint8_t)(1U<<bit);feed(&l,wire,size,0);wire[byte]^=(uint8_t)(1U<<bit);
    if(m.calls!=before) e|=128;
  }
  /* Any split across multiple poll iterations keeps exactly one frame. */
  for(unsigned split=0;split<=size;++split) {
    phone_init(&l,dispatch,send,&m);before=m.calls;
    feed(&l,wire,split,0);feed(&l,wire+split,size-split,100);
    if(m.calls!=before+1) e|=256;
  }
  phone_init(&l,dispatch,send,&m);before=m.calls;
  feed(&l,wire,3,0);feed(&l,wire+3,size-3,500);
  if(m.calls!=before) e|=512;
  feed(&l,wire,size,501);if(m.calls!=before+1) e|=1024;
  before=m.calls;phone_feed(&l,0,502);
  for(unsigned i=0;i<200;++i) phone_feed(&l,0x7f,502);
  phone_feed(&l,0,502);if(m.calls!=before) e|=2048;
  size=phone_pack(1,101,0,0,wire);feed(&l,wire,size,503);
  if(m.calls!=before+1) e|=4096;
  before=m.calls;phone_feed(&l,0,504);phone_feed(&l,2,504);phone_loss(&l);
  feed(&l,wire+1,size-1,504);if(m.calls!=before) e|=8192;
  size=phone_pack(1,102,0,0,wire);feed(&l,wire,size,505);
  if(m.calls!=before+1) e|=16384;
  phone_init(&l,dispatch,send,&m);before=m.calls;
  size=phone_pack(0x81,0,0,0,wire);feed(&l,wire,size,0);
  if(m.calls!=before) e|=32768;
  size=phone_pack(1,65535,0,0,wire);feed(&l,wire,size,UINT32_MAX-200);
  size=phone_pack(1,0,0,0,wire);feed(&l,wire,3,UINT32_MAX-100);
  feed(&l,wire+3,size-3,100);if(m.calls!=before+2) e|=65536;
  return e;
}
