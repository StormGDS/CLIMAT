#include "bt_boot.h"
#include <string.h>
static const uint32_t bauds[]={9600,19200,38400,57600,115200,4800,2400,1200};
void bt_boot_set_uart(BtBoot *b, int (*set_baud)(void *,uint32_t))
{b->set_baud=set_baud;}
unsigned bt_boot_busy(const BtBoot *b) {return b->state<BT_READY;}
const char *bt_boot_status(const BtBoot *b)
{
  if(bt_boot_busy(b)) return "CONFIGURING";
  switch(b->state) {
  case BT_READY:return "READY";case BT_NO_AT:return "NO_AT_RESPONSE";
  case BT_UNSUPPORTED:return "UNSUPPORTED_FIRMWARE";
  case BT_LINK_ACTIVE:return "DATA_ALREADY_ACTIVE";default:return "FAILED";
  }
}
void bt_boot_init(BtBoot *b, const uint8_t name[3], uint32_t now,
                  int (*send)(void *,const char *), void *context)
{
  memset(b,0,sizeof(*b));b->send=send;b->context=context;b->started=now;b->baud=9600;
  memcpy(b->name,"CLM-",4);
  static const char hex[]="0123456789ABCDEF";
  for(unsigned i=0;i<3;++i) {b->name[4+2*i]=hex[name[i]>>4];b->name[5+2*i]=hex[name[i]&15];}
}
static int setting(unsigned state)
{return state==BT_ROLE_SET || state==BT_NAME_SET || state==BT_UUID_SET || state==BT_CHAR_SET;}
static void enter(BtBoot *b, unsigned state, uint32_t now)
{
  b->state=b->phase=(uint8_t)state;b->used=0;b->response[0]=0;b->at=b->last_rx=now;b->retried=0;
  const char *command=0;char name_command[24];
  switch(state) {
  case BT_PROBE:case BT_WRITE_PROBE:command="AT\r\n";break;
  case BT_VERSION:command="AT+VERSION\r\n";break;
  case BT_ROLE_READ:case BT_ROLE_VERIFY:command="AT+ROLE\r\n";break;
  case BT_ROLE_SET:command="AT+ROLE0\r\n";break;
  case BT_NAME_READ:case BT_NAME_VERIFY:command="AT+NAME\r\n";break;
  case BT_NAME_SET:
    memcpy(name_command,"AT+NAME",7);memcpy(name_command+7,b->name,10);
    memcpy(name_command+17,"\r\n",3);command=name_command;break;
  case BT_UUID_READ:case BT_UUID_VERIFY:command="AT+UUID\r\n";break;
  case BT_UUID_SET:command="AT+UUIDFFE0\r\n";break;
  case BT_CHAR_READ:case BT_CHAR_VERIFY:command="AT+CHAR\r\n";break;
  case BT_CHAR_SET:command="AT+CHARFFE1\r\n";break;
  case BT_RESET_WAIT:command="AT+RESET\r\n";b->reset_done=1;break;
  default:break;
  }
  if(command) {
    if(!b->send || !b->send(b->context,command)) b->state=BT_FAILED;
    else if(setting(state)) ++b->writes;
  }
}
static void next_baud(BtBoot *b, uint32_t now)
{
  if(!b->set_baud) {b->state=BT_NO_AT;return;}
  if(++b->baud_index>=sizeof(bauds)/sizeof(bauds[0])) {
    b->baud=9600;
    b->state=b->set_baud(b->context,b->baud)?BT_NO_AT:BT_FAILED;
    return;
  }
  b->baud=bauds[b->baud_index];
  if(!b->set_baud(b->context,b->baud)) {b->state=BT_FAILED;return;}
  enter(b,BT_UART_WAIT,now);
}
static int line(const BtBoot *b, const char *prefix, const char *value)
{
  size_t p=strlen(prefix),v=strlen(value);
  for(unsigned i=0;i<b->used;++i) {
    if(i && b->response[i-1]!='\n' && b->response[i-1]!='\r') continue;
    if(i+p+v<=b->used && !memcmp(b->response+i,prefix,p) &&
       !memcmp(b->response+i+p,value,v) &&
       (i+p+v==b->used || b->response[i+p+v]=='\r' || b->response[i+p+v]=='\n')) return 1;
  }
  return 0;
}
static int value_present(const BtBoot *b, const char *prefix)
{
  size_t n=strlen(prefix);
  for(unsigned i=0;i+n<b->used;++i)
    if((!i || b->response[i-1]=='\r' || b->response[i-1]=='\n') &&
       !memcmp(b->response+i,prefix,n)) return 1;
  return 0;
}
static void verify_after_write(BtBoot *b, uint32_t now)
{
  b->verify_next=(uint8_t)(b->state+1);b->state=BT_WRITE_WAIT;b->at=now;b->used=0;
}
static void response(BtBoot *b, uint32_t now)
{
  /* A setter may reboot the module or emit an unreliable ACK. Verify actual
   * values after its restart/EEPROM settling time, regardless of its ACK. */
  if(setting(b->state)) {verify_after_write(b,now);return;}
  if(b->state==BT_PROBE && b->set_baud && !line(b,"","OK")) {
    b->used=0;b->response[0]=0;return;
  }
  if(line(b,"","ERROR")) {b->state=b->state==BT_VERSION?BT_UNSUPPORTED:BT_FAILED;return;}
  switch(b->state) {
  case BT_PROBE:
    if(line(b,"","OK")) {b->present=1;enter(b,BT_VERSION,now);} else b->state=BT_FAILED;
    return;
  case BT_WRITE_PROBE:
    if(line(b,"","OK")) enter(b,b->verify_next,now);else b->state=BT_FAILED;
    return;
  case BT_VERSION:
    if(line(b,"+VERSION=","v6.1")) enter(b,BT_ROLE_READ,now);
    else b->state=BT_UNSUPPORTED;
    return;
  case BT_ROLE_READ:case BT_ROLE_VERIFY:
    if(line(b,"+ROLE=","0")) enter(b,BT_NAME_READ,now);
    else if(b->state==BT_ROLE_READ && !b->reset_done && line(b,"+ROLE=","1")) enter(b,BT_ROLE_SET,now);
    else b->state=BT_FAILED;
    return;
  case BT_NAME_READ:case BT_NAME_VERIFY:
    if(line(b,"+NAME=",b->name)) enter(b,BT_UUID_READ,now);
    else if(b->state==BT_NAME_READ && !b->reset_done && value_present(b,"+NAME=")) enter(b,BT_NAME_SET,now);
    else b->state=BT_FAILED;
    return;
  case BT_UUID_READ:case BT_UUID_VERIFY:
    if(line(b,"+UUID=","FFE0")) enter(b,BT_CHAR_READ,now);
    else if(b->state==BT_UUID_READ && !b->reset_done && value_present(b,"+UUID=")) enter(b,BT_UUID_SET,now);
    else b->state=BT_FAILED;
    return;
  case BT_CHAR_READ:case BT_CHAR_VERIFY:
    if(line(b,"+CHAR=","FFE1")) enter(b,b->writes && !b->reset_done?BT_RESET_WAIT:BT_READY,now);
    else if(b->state==BT_CHAR_READ && !b->reset_done && value_present(b,"+CHAR=")) enter(b,BT_CHAR_SET,now);
    else b->state=BT_FAILED;
    return;
  default:b->state=BT_FAILED;return;
  }
}
void bt_boot_poll(BtBoot *b, uint32_t now)
{
  if(!bt_boot_busy(b)) return;
  if((uint32_t)(now-b->started)>=(b->set_baud?13000U:6500U)) {b->state=BT_FAILED;return;}
  if(b->state==BT_POWER_WAIT) {
    if((uint32_t)(now-b->started)>=500) enter(b,BT_PROBE,now);
    return;
  }
  if(b->state==BT_UART_WAIT) {
    if((uint32_t)(now-b->at)>=50) enter(b,BT_PROBE,now);
    return;
  }
  if(b->state==BT_RESET_WAIT) {
    if((uint32_t)(now-b->at)>=700) enter(b,BT_PROBE,now);
    return;
  }
  if(b->state==BT_WRITE_WAIT) {
    if((uint32_t)(now-b->at)>=700) enter(b,BT_WRITE_PROBE,now);
    return;
  }
  /* Process complete text only after all UART queue bytes have been fed. */
  if(b->used && (uint32_t)(now-b->last_rx)>=25) {response(b,now);return;}
  if((uint32_t)(now-b->at)<350) return;
  if(setting(b->state)) {verify_after_write(b,now);return;}
  if(b->state==BT_PROBE || b->state==BT_WRITE_PROBE) {
    if(!b->retried) {unsigned state=b->state;enter(b,state,now);b->retried=1;}
    else if(b->state==BT_PROBE && !b->present) next_baud(b,now);
    else b->state=b->state==BT_PROBE?BT_NO_AT:BT_FAILED;
  } else b->state=BT_FAILED;
}
int bt_boot_byte(BtBoot *b, uint8_t byte, uint32_t now)
{
  if(!bt_boot_busy(b)) return 0;
  /* A phone may already be connected across an MCU-only reset. Stop AT
   * configuration immediately and let a framed request reach the parser. */
  if(!byte && !(b->set_baud && !b->present && b->baud_index)) {b->state=BT_LINK_ACTIVE;return 0;}
  if(b->state==BT_POWER_WAIT || b->state==BT_UART_WAIT) return 1;
  if(b->state==BT_RESET_WAIT) return 1;
  if(b->state==BT_WRITE_WAIT) return 1;
  if(b->used==sizeof(b->response)-1 || (byte<32 && byte!='\r' && byte!='\n')) {
    if(b->set_baud && b->state==BT_PROBE && !b->present) {b->used=0;b->response[0]=0;return 1;}
    b->state=BT_FAILED;return 1;
  }
  b->response[b->used++]=(char)byte;b->response[b->used]=0;b->last_rx=now;return 1;
}
