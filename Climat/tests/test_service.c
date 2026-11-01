/* Real console + framed protocol + storage integration, no MCU registers. */
#include "service_console.h"
#include "phone_link.h"
#include <string.h>
static uint8_t flash[SETTINGS_SIZE],reply[PHONE_WIRE_MAX];
static unsigned reply_size,writes,last_size;
static uint8_t last_wire[PHONE_WIRE_MAX];
static int read_flash(uint8_t *p) {memcpy(p,flash,SETTINGS_SIZE);return 1;}
static int write_flash(const uint8_t *p) {memcpy(flash,p,SETTINGS_SIZE);++writes;return 1;}
static void text(const char *s) {(void)s;}
static void report(uint32_t now) {(void)now;}
static void send(const uint8_t *p,unsigned n) {memcpy(reply,p,n);reply_size=n;}
static unsigned request(unsigned op,const uint8_t *p,unsigned n,uint8_t *response,uint32_t now)
{
  static uint16_t seq;uint8_t wire[PHONE_WIRE_MAX];
  unsigned size=phone_pack((uint8_t)op,++seq,p,n,wire);reply_size=0;
  memcpy(last_wire,wire,size);last_size=size;
  for(unsigned i=0;i<size;++i) service_phone_byte(wire[i]);
  service_phone_poll(now);service_phone_poll(now);
  uint8_t raw[PHONE_RAW_MAX];unsigned decoded=phone_unstuff(reply+1,reply_size-2,raw);
  if(!reply_size || decoded<8 || raw[0]!=PHONE_PROTOCOL_VERSION ||
     decoded!=(unsigned)raw[4]+7 || raw[1]!=(op|0x80U) ||
     (unsigned)(raw[2]|((unsigned)raw[3]<<8))!=seq ||
     phone_crc16(raw,decoded-2)!=(unsigned)(raw[decoded-2]|((unsigned)raw[decoded-1]<<8))) return 0;
  memcpy(response,raw+5,raw[4]);return raw[4];
}
static void cli(const char *s,uint32_t now)
{while(*s) service_rx_byte((uint8_t)*s++);service_rx_byte('\n');service_poll(now);}
uint32_t service_selftest(void)
{
  uint32_t e=0;TemperatureProbe probe;ProbeBus bus={0};SettingsBackend io={read_flash,write_flash};
  Settings cfg;uint8_t p[64],saved[64],rom[16]={0};
  memset(flash,255,sizeof(flash));writes=0;
  probe_init(&probe,bus);service_init(&probe,report,text,io);service_phone_init(send);
  if(request(1,0,0,p,0)!=8 || p[1]!=3 || p[5]!=5) e|=1;
  if(request(4,0,0,p,0)!=40 || p[1]!=176 || p[3]!=8) e|=2;
  memcpy(saved,p+1,31);saved[16]=0xe8;saved[17]=3; /* 1000 mC/min */
  if(request(5,saved,31,p,0)!=1 || p[0]) e|=4;
  rom[0]=rom[8]=0x28;rom[1]=1;rom[9]=2;
  rom[7]=probe_crc8(rom,7);rom[15]=probe_crc8(rom+8,7);
  if(request(15,rom,16,p,0)!=1 || p[0] || !probe.sensor[0].assigned ||
     memcmp(probe.sensor[1].rom,rom+8,8)) e|=8;
  if(request(6,0,0,p,0)!=2 || p[0] || writes!=1) e|=16;
  if(request(6,0,0,p,0)!=2 || !p[1] || writes!=1) e|=32;
  /* A reset reloads explicit S1/S2 identities and never sorts them. */
  probe_init(&probe,bus);service_init(&probe,report,text,io);service_phone_init(send);
  if(request(14,0,0,p,0)!=17 || memcmp(p+1,rom,16) ||
     settings_load(io,&cfg)!=SETTINGS_OK || cfg.automatic.cooling_milli_per_min!=1000) e|=64;
  memcpy(saved,rom,16);memcpy(saved+8,saved,8);
  if(request(15,saved,16,p,0)!=1 || p[0]!=PHONE_BAD_CONFIG ||
     request(14,0,0,p,0)!=17 || memcmp(p+1,rom,16)) e|=128;
  saved[0]^=1;
  if(request(15,saved,16,p,0)!=1 || p[0]!=PHONE_BAD_CONFIG) e|=256;
  if(request(4,0,0,p,0)!=40) e|=512;
  memcpy(saved,p+1,31);saved[2]=saved[3]=0; /* zero hysteresis */
  if(request(5,saved,31,p,0)!=1 || p[0]!=PHONE_BAD_CONFIG) e|=512;
  if(request(4,0,0,p,0)!=40 || p[3]!=8 || p[17]!=0xe8 || p[18]!=3) e|=1024;
  /* CLI edits use the same model, including the full uint16 speed range. */
  cli("AUTO SET 176 8 168 8 400 16 560 16 65535",0);
  if(request(4,0,0,p,0)!=40 || p[17]!=255 || p[18]!=255) e|=2048;
  cli("AUTO SET 176 8 168 8 400 16 560 16 65536",0);
  if(request(4,0,0,p,0)!=40 || p[17]!=255 || p[18]!=255) e|=4096;
  cli("SENSOR CLEAR 1",0);
  if(request(14,0,0,p,0)!=17 || p[1] || memcmp(p+9,rom+8,8)) e|=8192;
  if(request(7,0,0,p,0)!=1 || p[0] || request(14,0,0,p,0)!=17 || memcmp(p+1,rom,16)) e|=16384;
  if(request(3,0,0,p,0)!=43) e|=32768;
  if(request(8,0,0,p,0)!=1 || p[0] || request(14,0,0,p,0)!=17 || p[1] || p[9]) e|=65536;
  if(request(7,0,0,p,0)!=1 || p[0] || request(14,0,0,p,0)!=17 || memcmp(p+1,rom,16)) e|=131072;
  probe.sensor[0].valid=1;probe.sensor[0].raw16=320;service_sample(100);
  uint8_t mode=1,relay[2]={1,1};
  if(request(9,&mode,1,p,100)!=6 || p[0] || request(10,relay,2,p,200)!=6 || p[0]) e|=262144;
  for(unsigned i=0;i<last_size;++i) service_phone_byte(last_wire[i]);
  service_phone_poll(1000);
  if(request(2,0,0,p,1001)!=23 || p[5]!=1 ||
     (uint32_t)(p[11]|((uint32_t)p[12]<<8)|((uint32_t)p[13]<<16)|((uint32_t)p[14]<<24))!=599199) e|=524288;
  service_phone_init(send);
  const uint8_t old_hello[]={0,4,1,1,1,1,3,0xd9,0xfa,0};
  for(unsigned i=0;i<sizeof(old_hello);++i) service_phone_byte(old_hello[i]);
  service_phone_poll(1002);uint8_t raw[PHONE_RAW_MAX];
  unsigned n=phone_unstuff(reply+1,reply_size-2,raw);
  if(n!=8 || raw[0]!=3 || raw[5]!=PHONE_VERSION) e|=1048576;
  return e;
}

#define REQUIRE(x) do {if(!(x)) return __LINE__;} while(0)
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static unsigned get16(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static uint8_t driven;
static uint8_t drive(uint8_t mask) {driven=mask;return mask;}
uint32_t service_rate_test(void)
{
  TemperatureProbe probe;ProbeBus bus={0};SettingsBackend io={read_flash,write_flash};
  uint8_t response[64],edit[31],baseline[31],bad[31];Settings s;
  memset(flash,255,sizeof(flash));writes=0;probe_init(&probe,bus);
  service_init(&probe,report,text,io);service_phone_init(send);service_set_relay_driver(drive);
  REQUIRE(request(4,0,0,response,0)==40);memcpy(edit,response+1,31);
  put16(edit+16,1000);put16(edit+23,1000);put16(edit+25,200);put16(edit+27,10);put16(edit+29,12);
  REQUIRE(request(5,edit,31,response,0)==1 && response[0]==PHONE_OK);
  REQUIRE(request(4,0,0,response,0)==40 && !memcmp(response+1,edit,31));
  REQUIRE(get16(response+32)==5 && get16(response+34)==168 && get16(response+36)==30 && get16(response+38)==60);
  REQUIRE(request(5,edit,23,response,0)==1 && response[0]==PHONE_BAD_PAYLOAD);
  memcpy(baseline,edit,31);
  const unsigned offsets[]={23,25,27,27,29,29};
  const unsigned values[]={100,1001,0,601,0,65535};
  for(unsigned i=0;i<6;++i) {
    memcpy(bad,baseline,31);put16(bad+offsets[i],values[i]);
    REQUIRE(request(5,bad,31,response,0)==1 && response[0]==PHONE_BAD_CONFIG);
    REQUIRE(request(4,0,0,response,0)==40 && !memcmp(response+1,baseline,31));
  }
  REQUIRE(request(6,0,0,response,0)==2 && !response[0] && writes==1);
  REQUIRE(request(6,0,0,response,0)==2 && response[1] && writes==1);
  REQUIRE(settings_load(io,&s)==SETTINGS_OK && s.automatic.rising_milli_per_min==1000 &&
    s.automatic.rate_hyst_milli_per_min==200 && s.heater_min_on_seconds==10 && s.fan_min_on_seconds==12);
  REQUIRE(request(8,0,0,response,0)==1 && !response[0]);
  REQUIRE(request(4,0,0,response,0)==40 && get16(response+24)==0 && get16(response+26)==0 && get16(response+28)==30 && get16(response+30)==60);
  REQUIRE(request(7,0,0,response,0)==1 && !response[0]);
  REQUIRE(request(4,0,0,response,0)==40 && !memcmp(response+1,baseline,31));
  service_init(&probe,report,text,io);service_phone_init(send);service_set_relay_driver(drive);
  REQUIRE(request(4,0,0,response,0)==40 && !memcmp(response+1,baseline,31));
  /* Actual dispatcher->RAM->AUTO->arbiter->fake relay. Same temperatures, R A/B. */
  for(unsigned blocked=0;blocked<2;++blocked) {
    memcpy(edit,baseline,31);put16(edit+23,blocked?1000:0);
    uint32_t base=blocked*100000U;
    REQUIRE(request(5,edit,31,response,base)==1 && !response[0]);
    for(unsigned sec=0;sec<=45;++sec) {
      probe.sensor[0].valid=1;probe.sensor[0].raw16=160;probe.sensor[0].trend_valid=1;
      probe.sensor[0].rate_milli_per_min=1500;service_sample(base+sec*1000);
      if(sec>=42) REQUIRE(driven==(blocked?1:3));
      REQUIRE(!((driven&3) && (driven&12)));
    }
  }
  memcpy(edit,baseline,31);put16(edit+23,65535);put16(edit+25,1000);put16(edit+27,600);put16(edit+29,1);
  REQUIRE(request(5,edit,31,response,145000)==1 && !response[0]);
  REQUIRE(request(4,0,0,response,145000)==40 && get16(response+24)==65535 && get16(response+26)==1000 && get16(response+28)==600 && get16(response+30)==1);
  service_phone_init(send);
  const uint8_t legacy_v2[]={0,4,2,1,77,1,3,199,124,0};
  for(unsigned i=0;i<sizeof(legacy_v2);++i) service_phone_byte(legacy_v2[i]);
  service_phone_poll(145000);uint8_t raw[PHONE_RAW_MAX];
  REQUIRE(phone_unstuff(reply+1,reply_size-2,raw)==8 && raw[0]==3 && raw[5]==PHONE_VERSION);
  service_set_relay_driver(0);
  return 0;
}
