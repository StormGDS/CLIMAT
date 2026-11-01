#include "service_console.h"
#include "control_selftest.h"
#include "tiny_format_selftest.h"
#include "auto_control.h"
#include "phone_link.h"
#include "thermal_timing.h"
#include "bt_boot.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef CLIMAT_COMPACT
#include "tiny_format.h"
#define snprintf tiny_snprintf
#define CLI_TEXT(compact,diagnostic) compact
#else
#define CLI_TEXT(compact,diagnostic) diagnostic
#endif
static TemperatureProbe *probe;
static ControlCore control;
static AutoControl automatic;
static uint8_t (*relay_driver)(uint8_t);
static uint8_t physical_mask;
static void apply_relays(void)
{physical_mask=relay_driver?(uint8_t)(relay_driver(control.logical)&15U):0;}
void service_set_relay_driver(uint8_t (*driver)(uint8_t))
{
  if(relay_driver && relay_driver!=driver) (void)relay_driver(0);
  relay_driver=driver;apply_relays();
}
static void tick(uint32_t now) {control_tick(&control,now);apply_relays();}
static uint8_t device_name[3];
static SettingsBackend storage;
static int (*bt_handler)(const char *);
void service_set_bt_handler(int (*handler)(const char *)) {bt_handler=handler;}
static void (*report_fn)(uint32_t);
static void (*out)(const char *);
static volatile uint8_t queue[128], head, tail, damaged;
static char line[96];
static unsigned used, discard, stream;
static PhoneLink phone;
static void (*phone_output)(const uint8_t *,unsigned);
static volatile uint8_t phone_queue[128],phone_head,phone_tail,phone_damaged;
static uint8_t phone_test;
static int (*phone_filter)(uint8_t,uint32_t);
void service_set_phone_filter(int (*filter)(uint8_t,uint32_t)) {phone_filter=filter;}
void service_device_name(uint8_t name[3]) {memcpy(name,device_name,3);}
static void status(uint32_t now);
static const char *settings_reason(SettingsResult r)
{
  return r==SETTINGS_OK?"VALID":r==SETTINGS_EMPTY?"EMPTY":r==SETTINGS_INVALID?"INVALID":"IO_ERROR";
}
static void settings_current(Settings *s)
{
  s->heater_min_on_seconds=control.heater_min_on_seconds;s->fan_min_on_seconds=control.fan_min_on_seconds;
  s->automatic=automatic.config;s->alternate=automatic.alternate;
  s->manual_seconds=(uint16_t)(control.timeout_ms/1000);
  s->fail_count=control.fail_count;s->recover_count=control.recover_count;
  memcpy(s->device_name,device_name,3);
  for(unsigned i=0;i<2;++i) memcpy(s->sensor_rom[i],probe->sensor[i].rom,8);
}
static void settings_apply(const Settings *s, uint32_t now)
{
  uint8_t current_rom[2][8];
  for(unsigned i=0;i<2;++i) memcpy(current_rom[i],probe->sensor[i].rom,8);
  if(memcmp(current_rom,s->sensor_rom,16)) {
    (void)probe_set_bindings(probe,s->sensor_rom);
    control.sampled=0;control.logical=0;
    memset(control.valid,0,sizeof(control.valid));
  }
  (void)auto_configure(&automatic,&s->automatic);automatic.alternate=s->alternate;
  automatic.lead=0;
  control.fail_count=s->fail_count;control.recover_count=s->recover_count;
  control.timeout_ms=(uint32_t)s->manual_seconds*1000;
  control.heater_min_on_seconds=s->heater_min_on_seconds;control.fan_min_on_seconds=s->fan_min_on_seconds;
  memcpy(device_name,s->device_name,3);
  memset(control.failures,0,sizeof(control.failures));
  memset(control.successes,0,sizeof(control.successes));control.source=0;
  control.auto_requested=0;control_mode(&control,0,now);apply_relays();
}
static void set_mode(unsigned manual, uint32_t now)
{auto_reset(&automatic);control.auto_requested=0;control_mode(&control,manual,now);apply_relays();}
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void w16(uint8_t *p, uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void w32(uint8_t *p, uint32_t n) {w16(p,(uint16_t)n);w16(p+2,(uint16_t)(n>>16));}
static void config_payload(const Settings *s, uint8_t *p)
{
  auto_config_write(&s->automatic,p);
  p[18]=s->fail_count;p[19]=s->recover_count;p[20]=s->alternate;w16(p+21,s->manual_seconds);
  w16(p+23,s->automatic.rising_milli_per_min);w16(p+25,s->automatic.rate_hyst_milli_per_min);
  w16(p+27,s->heater_min_on_seconds);w16(p+29,s->fan_min_on_seconds);
}
static unsigned phone_dispatch(void *context, uint8_t op, const uint8_t *p,
                               unsigned n, uint8_t *r, uint32_t now)
{
  (void)context;Settings s;uint8_t encoded[SETTINGS_SIZE];
  tick(now);r[0]=PHONE_OK;
  if(op>=1 && op<=15 && op!=5 && op!=9 && op!=10 && op!=13 && op!=15 && n) {r[0]=PHONE_BAD_PAYLOAD;return 1;}
  switch(op) {
  case 1: /* HELLO */
    r[1]=PHONE_PROTOCOL_VERSION;r[2]=PHONE_PAYLOAD_MAX;r[3]=TREND_WINDOW_SECONDS;
    r[4]=(uint8_t)(14U | (relay_driver?0U:1U)); /* lock + Flash + name + bindings */
    r[5]=SETTINGS_VERSION;r[6]=2;r[7]=4;return 8;
  case 2: { /* STATUS */
    w32(r+1,now);r[5]=control.manual;
    r[6]=control.sampled && !control.fault && control.valid[control.source]?control.source+1:0;
    r[7]=(uint8_t)(control.fault | (control.sampled<<1) | (relay_driver?0U:4U) |
                  (automatic.configured<<3) | (automatic.rate_ready<<4));
    r[8]=control.manual?control.requested:control.auto_requested;r[9]=control.logical;r[10]=physical_mask;
    uint32_t elapsed=now-control.manual_at;
    w32(r+11,control.manual && elapsed<control.timeout_ms?control.timeout_ms-elapsed:0);
    w32(r+15,control.sampled?now-control.sample_at:UINT32_MAX);
    w32(r+19,(uint32_t)automatic.effective_milli);return 23;
  }
  case 3: /* SENSORS: flags, ROM, raw16, rate, errors per slot */
    w32(r+1,now);
    for(unsigned i=0;i<2;++i) {
      ProbeSensor *s=&probe->sensor[i];uint8_t *d=r+5+19*i;
      d[0]=(uint8_t)(s->assigned | (s->valid<<1) | (s->trend_valid<<2));
      memcpy(d+1,s->rom,8);w16(d+9,(uint16_t)s->raw16);
      w32(d+11,(uint32_t)s->rate_milli_per_min);
      w32(d+15,s->errors);
    }return 43;
  case 4:
    settings_current(&s);config_payload(&s,r+1);
    w16(r+32,TREND_WINDOW_SECONDS);w16(r+34,AUTO_MIN_RAW16);
    w16(r+36,30);w16(r+38,60);return 40;
  case 5: { /* Atomic edit, no implicit SAVE. */
    if(n!=31) {r[0]=PHONE_BAD_PAYLOAD;return 1;}
    settings_current(&s);auto_config_read(&s.automatic,p);
    s.fail_count=p[18];s.recover_count=p[19];s.alternate=p[20];
    s.manual_seconds=u16(p+21);
    s.automatic.rising_milli_per_min=u16(p+23);s.automatic.rate_hyst_milli_per_min=u16(p+25);
    s.heater_min_on_seconds=u16(p+27);s.fan_min_on_seconds=u16(p+29);
    memcpy(s.device_name,device_name,3); /* SET_CONFIG preserves the separate name. */
    if(!settings_encode(&s,encoded)) {r[0]=PHONE_BAD_CONFIG;return 1;}
    settings_apply(&s,now);return 1;
  }
  case 6: {
    unsigned unchanged;settings_current(&s);
    SettingsResult result=settings_save(storage,&s,&unchanged);
    if(result!=SETTINGS_OK) {r[0]=PHONE_STORAGE_ERROR;r[1]=(uint8_t)result;return 2;}
    r[1]=(uint8_t)unchanged;return 2;
  }
  case 7: {
    SettingsResult result=settings_load(storage,&s);
    if(result!=SETTINGS_OK) {r[0]=PHONE_STORAGE_ERROR;r[1]=(uint8_t)result;return 2;}
    settings_apply(&s,now);return 1;
  }
  case 8: settings_defaults(&s);settings_apply(&s,now);return 1;
  case 9:
    if(n!=1 || p[0]>1) {r[0]=PHONE_BAD_PAYLOAD;return 1;}
    set_mode(p[0],now);break;
  case 10:
    if(n!=2 || p[0]<1 || p[0]>4 || p[1]>1) {r[0]=PHONE_BAD_PAYLOAD;return 1;}
    if(!control_relay(&control,p[0],p[1],now)) r[0]=PHONE_DENIED;
    break;
  case 11: {
    SettingsResult result=settings_load(storage,&s);settings_current(&s);
    r[1]=(uint8_t)result;r[2]=(uint8_t)(result==SETTINGS_OK && storage.read(encoded));
    if(r[2]) {uint8_t current[SETTINGS_SIZE];settings_encode(&s,current);r[2]=(uint8_t)!memcmp(encoded,current,SETTINGS_SIZE);}
    return 3;
  }
  case 12:memcpy(r+1,device_name,3);return 4;
  case 13:
    if(n!=3) {r[0]=PHONE_BAD_PAYLOAD;return 1;}
    memcpy(device_name,p,3);return 1;
  case 14:
    settings_current(&s);memcpy(r+1,s.sensor_rom,16);return 17;
  case 15:
    if(n!=16) {r[0]=PHONE_BAD_PAYLOAD;return 1;}
    settings_current(&s);memcpy(s.sensor_rom,p,16);
    if(!settings_encode(&s,encoded)) {r[0]=PHONE_BAD_CONFIG;return 1;}
    settings_apply(&s,now);return 1;
  default:r[0]=PHONE_BAD_OP;return 1;
  }
  apply_relays();
  r[1]=control.manual;r[2]=control.manual?control.requested:control.auto_requested;
  r[3]=control.logical;r[4]=control.fault;r[5]=(uint8_t)(relay_driver==0);return 6;
}
static void phone_send(void *context, const uint8_t *p, unsigned n)
{
  (void)context;
  if(phone_test) {
    char text[3];out("PHONE TX ");
    for(unsigned i=0;i<n;++i) {(void)snprintf(text,sizeof(text),"%02X",p[i]);out(text);}out("\r\n");
  } else if(phone_output) phone_output(p,n);
}
void service_phone_init(void (*send)(const uint8_t *,unsigned))
{phone_output=send;phone_init(&phone,phone_dispatch,phone_send,0);}
void service_phone_byte(uint8_t b)
{
  uint8_t next=(uint8_t)((phone_head+1U)&127U);
  if(next==phone_tail) {phone_damaged=1;return;}
  phone_queue[phone_head]=b;phone_head=next;
}
void service_phone_loss(void) {phone_damaged=1;}
void service_phone_poll(uint32_t now)
{
  phone_tick(&phone,now);
  if(phone_damaged) {phone_damaged=0;phone_tail=phone_head;phone_loss(&phone);}
  if(phone_test) {phone_tail=phone_head;return;}
  for(unsigned budget=0;budget<64 && phone_tail!=phone_head;++budget) {
    uint8_t b=phone_queue[phone_tail];phone_tail=(uint8_t)((phone_tail+1U)&127U);
    if(phone_filter && phone_filter(b,now)) continue;
    phone_feed(&phone,b,now);
    if(phone_damaged) {phone_damaged=0;phone_tail=phone_head;phone_loss(&phone);break;}
  }
}
static int hex_digit(char c)
{return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}

void service_rx_byte(uint8_t b)
{
  uint8_t next=(uint8_t)((head+1U)&127U);
  if(next==tail) { damaged=1; return; }
  queue[head]=b; head=next;
}
void service_rx_error(void) { damaged=1; }
unsigned service_stream_enabled(void) { return stream; }
void service_init(TemperatureProbe *p, void (*r)(uint32_t), void (*o)(const char *), SettingsBackend io)
{
  probe=p; report_fn=r; out=o; storage=io;
  if(relay_driver) (void)relay_driver(0);
  relay_driver=0;physical_mask=0;control_init(&control,0);
  Settings s;SettingsResult result=settings_load(storage,&s);
  if(result!=SETTINGS_OK) settings_defaults(&s);
  settings_apply(&s,0);
  out(CLI_TEXT("BOOT ","SETTINGS BOOT source="));out(result==SETTINGS_OK?"FLASH":"DEFAULT");
  out(" reason=");out(settings_reason(result));out("\r\n");
}
void service_sample(uint32_t now)
{
  control_observe(&control,probe->sensor[0].valid,probe->sensor[1].valid,now);
  ProbeSensor *s=&probe->sensor[control.source];
  control.auto_requested=auto_sample(&automatic,control.source,
    s->valid && !control.manual,s->raw16,s->trend_valid,s->rate_milli_per_min,
    control.logical,now);
  tick(now);
  if(stream) status(now);
}
static void status(uint32_t now)
{
  char s[224];
  unsigned source=control.sampled && !control.fault && control.valid[control.source] ? control.source+1 : 0;
  uint32_t elapsed=now-control.manual_at;
  uint32_t left=control.manual && elapsed<control.timeout_ms ? (control.timeout_ms-elapsed+999)/1000 : 0;
  (void)snprintf(s,sizeof(s),
    CLI_TEXT("STATUS %s S%u F%u R%X O%X LOCKED %lu %s %s\r\n",
    "STATUS mode=%s source=S%u fault=%u requested=0x%X logical=0x%X physical=%s mask=0x%X manual_left=%lu s auto=%s failover=%s\r\n"),
    control.manual?"MANUAL":"AUTO",source,(unsigned)control.fault,
    (unsigned)(control.manual?control.requested:control.auto_requested),(unsigned)control.logical,
#ifndef CLIMAT_COMPACT
    relay_driver?"ENABLED":"LOCKED",(unsigned)physical_mask,
#endif
    (unsigned long)left,
    automatic.configured?"CONFIGURED":"UNCONFIGURED",
    control.fail_count && control.recover_count?"CONFIGURED":"UNCONFIGURED");out(s);
}
static void auto_status(void)
{
  const AutoConfig *p=&automatic.config; char s[224];
  (void)snprintf(s,sizeof(s),
    CLI_TEXT("AUTO %u %d,%d,%d,%d,%d,%d,%d,%d %u %u %ld %u %u\r\n",
    "AUTO configured=%u on_hyst16=%d,%d,%d,%d,%d,%d,%d,%d cooling_mC_min=%u rate_ready=%u effective_mC=%ld lead=K%u alternate=%u APPLY=RAM SAVE=FLASH\r\n"),
    automatic.configured,p->heat1_on,p->heat1_hyst,p->heat2_on,p->heat2_hyst,
    p->cool1_on,p->cool1_hyst,p->cool2_on,p->cool2_hyst,p->cooling_milli_per_min,automatic.rate_ready,
    (long)automatic.effective_milli,automatic.lead+3,automatic.alternate);out(s);
}
static int auto_settings(const char *s)
{
  int32_t v[9];
  for(unsigned i=0;i<9;++i) {
    while(*s==' ') ++s;
    if(!*s || (*s!='-' && *s!='+' && (*s<'0'||*s>'9'))) return 0;
    unsigned negative=*s=='-';if(*s=='-' || *s=='+') ++s;
    if(*s<'0' || *s>'9') return 0;
    unsigned n=0;
    while(*s>='0' && *s<='9') {n=n*10+(unsigned)(*s++-'0');if(n>(i==8?65535U:2000U)) return 0;}
    if((negative && (n>880 || i==8)) || (*s && *s!=' ')) return 0;
    v[i]=negative?-(int32_t)n:(int32_t)n;
  }
  while(*s==' ') ++s;
  if(*s) return 0;
  AutoConfig p={v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],automatic.config.rising_milli_per_min,automatic.config.rate_hyst_milli_per_min};
  return auto_configure(&automatic,&p);
}
static int number(const char *s, unsigned *n)
{
  if(!*s) return 0;
  unsigned v=0;
  while(*s) { if(*s<'0'||*s>'9'||v>3600) return 0; v=v*10+(unsigned)(*s++-'0'); }
  *n=v; return 1;
}
static void command(uint32_t now)
{
  if(!strcmp(line,"HELP")) {
#ifdef CLIMAT_COMPACT
    out("HELP: see PROTOCOL.md\r\n");
#else
    out("HELP STATUS SENSORS RELAYS STREAM ON|OFF MODE AUTO|MANUAL RELAY 1..4 ON|OFF\r\n"
        "CONFIG GET; CONFIG SET FAIL_COUNT 1..255; CONFIG SET RECOVER_COUNT 1..255\r\n"
        "CONFIG SET MANUAL_TIMEOUT 1..3600 (seconds, RAM only)\r\n"
        "CONFIG SET DEVICE_NAME ABC (3 printable bytes); CONFIG SET DEVICE_NAME_HEX 000000\r\n"
        "AUTO GET; AUTO SET h1on h1hyst h2on h2hyst c1on c1hyst c2on c2hyst speed (1/16 C; speed mC/min); AUTO ALTERNATE ON|OFF\r\n"
        "SENSOR BIND 1|2 ROMHEX16; SENSOR CLEAR 1|2 (RAM only; SAVE persists)\r\n"
        "SAVE (RAM to Flash); LOAD (Flash to RAM, AUTO); DEFAULT (test defaults to RAM, AUTO); CONFIG STORAGE\r\n"
        "STATUS reports physical output enable and mask.\r\n");
    out("BT STATUS; BT AT baud; BT ATLF baud (AT with CRLF; MCU baud only, no module settings writes)\r\n");
    out("BT VERSION; BT QUERY ROLE|NAME|UUID|CHAR|BAUD|LADDR (local maintenance)\r\n");
    out("PHONE STATUS; PHONE TEST ON|OFF; PHONE HEX encoded_frame_hex (bench transport)\r\n");
#endif
  }
#ifndef CLIMAT_PRODUCTION
  else if(!strcmp(line,"SELFTEST")) {
    uint32_t errors=control_selftest();char s[64];
    (void)snprintf(s,sizeof(s),"SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
    errors=auto_selftest();
    (void)snprintf(s,sizeof(s),"AUTO_SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
    errors=settings_selftest();
    (void)snprintf(s,sizeof(s),"SETTINGS_SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
    errors=probe_trend_selftest();
    (void)snprintf(s,sizeof(s),"TREND_SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
    errors=phone_selftest();
    (void)snprintf(s,sizeof(s),"PHONE_SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
    errors=bt_boot_selftest();
    (void)snprintf(s,sizeof(s),"BT_BOOT_SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
    errors=tiny_format_selftest();
    (void)snprintf(s,sizeof(s),"FORMAT_SELFTEST %s failures=0x%lX\r\n",errors?"FAIL":"PASS",(unsigned long)errors);out(s);
  }
  else if(!strcmp(line,"PHONE STATUS")) {
    char s[144];(void)snprintf(s,sizeof(s),"PHONE test=%u accepted=%lu rejected=%lu duplicates=%lu\r\n",
      phone_test,(unsigned long)phone.accepted,(unsigned long)phone.rejected,(unsigned long)phone.duplicates);out(s);
  } else if(!strcmp(line,"PHONE TEST ON") || !strcmp(line,"PHONE TEST OFF")) {
    phone_test=(uint8_t)!strcmp(line,"PHONE TEST ON");phone_tail=phone_head;phone_damaged=0;
    phone_init(&phone,phone_dispatch,phone_send,0);out(CLI_TEXT("OK RAM\r\n","OK PHONE TRANSPORT RESET\r\n"));
  } else if(!strncmp(line,"PHONE HEX ",10)) {
    const char *hex=line+10;unsigned length=(unsigned)strlen(hex);
    if(!phone_test || !length || length%2) {out(CLI_TEXT("ERR\r\n","ERR PHONE_TEST_OR_HEX\r\n"));return;}
    for(unsigned i=0;i<length;++i) if(hex_digit(hex[i])<0) {out(CLI_TEXT("ERR\r\n","ERR HEX\r\n"));return;}
    for(unsigned i=0;i<length;i+=2) {
      phone_feed(&phone,(uint8_t)((hex_digit(hex[i])<<4)|hex_digit(hex[i+1])),now);
      if(phone_damaged) {phone_damaged=0;phone_tail=phone_head;phone_loss(&phone);break;}
    }
    out(CLI_TEXT("OK RAM\r\n","OK PHONE FEED\r\n"));
  }
#endif
  else if(!strncmp(line,"BT ",3)) {
    if(!bt_handler || !bt_handler(line)) out(CLI_TEXT("ERR\r\n","ERR BT_SYNTAX_OR_BAUD\r\n"));
  } else if(!strcmp(line,"AUTO GET")) auto_status();
  else if(!strcmp(line,"AUTO ALTERNATE ON") || !strcmp(line,"AUTO ALTERNATE OFF")) {
    automatic.alternate=(uint8_t)!strcmp(line,"AUTO ALTERNATE ON");
    out(CLI_TEXT("OK RAM\r\n","OK RAM_ONLY\r\n"));auto_status();
  }
  else if(!strncmp(line,"AUTO SET ",9)) {
    if(!auto_settings(line+9)) {out(CLI_TEXT("ERR\r\n","ERR AUTO_SYNTAX_OR_THRESHOLDS; previous configuration retained\r\n"));return;}
    control.auto_requested=0;
    out(CLI_TEXT("OK RAM\r\n","OK AUTO RAM_ONLY\r\n"));auto_status();
  } else if(!strcmp(line,"STATUS") || !strcmp(line,"RELAYS")) status(now);
  else if(!strcmp(line,"SENSORS")) {
    report_fn(now);
    for(unsigned i=0;i<probe->discovered_count;++i) {
      out("FOUND ");char h[3];
      for(unsigned j=0;j<8;++j) {snprintf(h,sizeof(h),"%02X",probe->discovered[i][j]);out(h);}
      out("\r\n");
    }
  } else if(!strncmp(line,"SENSOR BIND ",12) || !strncmp(line,"SENSOR CLEAR ",13)) {
    unsigned clear=line[7]=='C', pos=clear?13:12;
    if(line[pos]<'1' || line[pos]>'2' ||
       (clear ? line[pos+1]!=0 : (line[pos+1]!=' ' || strlen(line+pos+2)!=16))) {
      out(CLI_TEXT("ERR\r\n","ERR SENSOR_SYNTAX\r\n"));return;
    }
    Settings s;settings_current(&s);unsigned slot=(unsigned)(line[pos]-'1');
    memset(s.sensor_rom[slot],0,8);
    if(!clear) for(unsigned i=0;i<8;++i) {
      int h=hex_digit(line[pos+2+2*i]),l=hex_digit(line[pos+3+2*i]);
      if(h<0 || l<0) {out(CLI_TEXT("ERR\r\n","ERR HEX\r\n"));return;}
      s.sensor_rom[slot][i]=(uint8_t)((h<<4)|l);
    }
    if(!probe_bindings_valid(s.sensor_rom)) {out(CLI_TEXT("ERR\r\n","ERR ROM_CRC_FAMILY_OR_DUPLICATE\r\n"));return;}
    settings_apply(&s,now);out(CLI_TEXT("OK RAM\r\n","OK BINDINGS RAM_ONLY MODE_AUTO\r\n"));
  }
  else if(!strcmp(line,"STREAM ON") || !strcmp(line,"STREAM OFF")) {
    stream=!strcmp(line,"STREAM ON");out(CLI_TEXT("OK RAM\r\n","OK STREAM\r\n"));
  } else if(!strcmp(line,"MODE AUTO") || !strcmp(line,"MODE MANUAL")) {
    set_mode(!strcmp(line,"MODE MANUAL"),now);status(now);
  } else if(!strncmp(line,"RELAY ",6)) {
    if(line[6]<'1'||line[6]>'4'||line[7]!=' ' ||
       (strcmp(line+8,"ON") && strcmp(line+8,"OFF"))) {out(CLI_TEXT("ERR\r\n","ERR SYNTAX\r\n"));return;}
    int accepted=control_relay(&control,(unsigned)(line[6]-'0'),!strcmp(line+8,"ON"),now);
    apply_relays();
    if(!accepted)
      out(CLI_TEXT("ERR DENIED\r\n","ERR MANUAL_REQUIRED_OR_FAULT_OR_INTERLOCK\r\n"));
    else if(relay_driver) out(CLI_TEXT("OK RAM\r\n","OK REQUEST; outputs follow arbiter; MANUAL immediate\r\n"));
    else out(CLI_TEXT("OK LOCKED\r\n","OK REQUEST dry_run=1; MANUAL immediate\r\n"));
    status(now);
  } else if(!strcmp(line,"CONFIG GET")) {
    char s[224];(void)snprintf(s,sizeof(s),
      CLI_TEXT("CONFIG %u %u %lu ON=%u,%u R=%u H=%u\r\n",
      "CONFIG FAIL_COUNT=%u RECOVER_COUNT=%u MANUAL_TIMEOUT=%lu APPLY=RAM SAVE=FLASH MIN_OFF=30,60 MIN_ON_HEAT=%u MIN_ON_FAN=%u R=%u H=%u\r\n"),
      (unsigned)control.fail_count,(unsigned)control.recover_count,(unsigned long)(control.timeout_ms/1000),
      control.heater_min_on_seconds,control.fan_min_on_seconds,
      automatic.config.rising_milli_per_min,automatic.config.rate_hyst_milli_per_min);out(s);
    out(CLI_TEXT("WINDOW=5\r\n","TIMING TREND_WINDOW=5 s; measured control\r\n"));
    (void)snprintf(s,sizeof(s),CLI_TEXT("NAME=%02X%02X%02X\r\n","DEVICE_NAME_HEX=%02X%02X%02X DEVICE_NAME_TEXT=%c%c%c\r\n"),
      device_name[0],device_name[1],device_name[2]
#ifndef CLIMAT_COMPACT
      ,device_name[0]>=33 && device_name[0]<=126?device_name[0]:'.',
      device_name[1]>=33 && device_name[1]<=126?device_name[1]:'.',
      device_name[2]>=33 && device_name[2]<=126?device_name[2]:'.'
#endif
      );out(s);
  } else if(!strncmp(line,"CONFIG SET ",11)) {
    char *key=line+11,*value=strchr(key,' '); unsigned n;
    if(!value) {out(CLI_TEXT("ERR\r\n","ERR SYNTAX\r\n"));return;} *value++=0;
    if(!strcmp(key,"DEVICE_NAME")) {
      if(strlen(value)!=3 || value[0]<33 || value[0]>126 || value[1]<33 || value[1]>126 ||
         value[2]<33 || value[2]>126) {out(CLI_TEXT("ERR\r\n","ERR NAME_REQUIRES_3_PRINTABLE_BYTES\r\n"));return;}
      memcpy(device_name,value,3);out(CLI_TEXT("OK RAM\r\n","OK RAM_ONLY\r\n"));return;
    }
    if(!strcmp(key,"DEVICE_NAME_HEX")) {
      uint8_t name[3];
      if(strlen(value)!=6) {out(CLI_TEXT("ERR\r\n","ERR NAME_REQUIRES_6_HEX_DIGITS\r\n"));return;}
      for(unsigned i=0;i<3;++i) {
        int high=hex_digit(value[2*i]),low=hex_digit(value[2*i+1]);
        if(high<0 || low<0) {out(CLI_TEXT("ERR\r\n","ERR HEX\r\n"));return;}
        name[i]=(uint8_t)((high<<4)|low);
      }
      memcpy(device_name,name,3);out(CLI_TEXT("OK RAM\r\n","OK RAM_ONLY\r\n"));return;
    }
    if(!number(value,&n)) {out(CLI_TEXT("ERR\r\n","ERR RANGE\r\n"));return;}
    if(!strcmp(key,"FAIL_COUNT") && n>=1 && n<=255) control.fail_count=(uint8_t)n;
    else if(!strcmp(key,"RECOVER_COUNT") && n>=1 && n<=255) control.recover_count=(uint8_t)n;
    else if(!strcmp(key,"MANUAL_TIMEOUT") && n>=1 && n<=3600) control.timeout_ms=n*1000;
    else {out(CLI_TEXT("ERR\r\n","ERR KEY_OR_RANGE\r\n"));return;}
    out(CLI_TEXT("OK RAM\r\n","OK RAM_ONLY\r\n"));
  } else if(!strcmp(line,"CONFIG STORAGE")) {
    Settings saved, current;uint8_t a[SETTINGS_SIZE],b[SETTINGS_SIZE];
    SettingsResult result=settings_load(storage,&saved);settings_current(&current);
    unsigned same=result==SETTINGS_OK && storage.read(a) &&
                  settings_encode(&current,b) && !memcmp(a,b,SETTINGS_SIZE);
    out(CLI_TEXT("STORE ","STORAGE flash="));out(settings_reason(result));out(same?CLI_TEXT(" =1\r\n"," ram_matches=1\r\n"):CLI_TEXT(" =0\r\n"," ram_matches=0\r\n"));
  } else if(!strcmp(line,"SAVE")) {
    Settings s;unsigned unchanged;settings_current(&s);
    out(CLI_TEXT("BUSY SAVE\r\n","BUSY SAVE; wait for OK/ERR before next command\r\n"));
    SettingsResult result=settings_save(storage,&s,&unchanged);
    /* UART is paused by the board adapter during erase/programming. */
    tail=head;damaged=0;
    if(result==SETTINGS_OK) out(unchanged?"OK SAVE UNCHANGED\r\n":"OK SAVE VERIFIED\r\n");
    else {out("ERR SAVE ");out(settings_reason(result));out(CLI_TEXT(" RAM\r\n"," RAM_RETAINED\r\n"));}
  } else if(!strcmp(line,"LOAD")) {
    Settings s;SettingsResult result=settings_load(storage,&s);
    if(result==SETTINGS_OK) {settings_apply(&s,now);out(CLI_TEXT("OK RAM\r\n","OK LOAD FLASH_TO_RAM MODE_AUTO\r\n"));}
    else {out("ERR LOAD ");out(settings_reason(result));out(CLI_TEXT(" RAM\r\n"," RAM_RETAINED\r\n"));}
  } else if(!strcmp(line,"DEFAULT")) {
    Settings s;settings_defaults(&s);settings_apply(&s,now);
    out(CLI_TEXT("OK RAM\r\n","OK DEFAULT RAM_ONLY MODE_AUTO; SAVE required to persist\r\n"));
  }
  else out(CLI_TEXT("ERR\r\n","ERR UNKNOWN_COMMAND\r\n"));
}
void service_poll(uint32_t now)
{
  tick(now);
  if(control.fault) auto_reset(&automatic);
  /* Limit work even under a continuous sender; no dynamic allocation. */
  for(unsigned budget=0;budget<128;++budget) {
    if(damaged) {damaged=0;tail=head;used=0;discard=1;out(CLI_TEXT("ERR\r\n","ERR RX_LOSS_RESEND_AFTER_NEWLINE\r\n"));}
    if(tail==head) break;
    uint8_t b=queue[tail]; tail=(uint8_t)((tail+1U)&127U);
    if(b=='\r'||b=='\n') {
      if(!discard && used) {line[used]=0;command(now);}
      used=0;discard=0;
    } else if(!discard) {
      if(b==8||b==127) {if(used) --used;}
      else if(b>=32 && b<=126 && used<sizeof(line)-1) line[used++]=(char)b;
      else {used=0;discard=1;out(CLI_TEXT("ERR\r\n","ERR LINE_TOO_LONG_OR_BINARY\r\n"));}
    }
  }
}
