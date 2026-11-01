#include "bt_boot.h"
#include <string.h>
#include <stdio.h>
typedef struct {
  char reply[64],name[11],uuid[5],characteristic[5];
  unsigned role,calls,pending,drop_ack,ignore_role,fail_tx,version_variant;
  uint32_t uart_baud,module_baud;
  unsigned baud_calls,fail_baud,noise,profile_error;
} Module;
static void module_init(Module *m)
{
  memset(m,0,sizeof(*m));m->uart_baud=m->module_baud=9600;memcpy(m->name,"CLM-413031",11);
  memcpy(m->uuid,"FFE0",5);memcpy(m->characteristic,"FFE1",5);
}
static int module_send(void *context, const char *cmd)
{
  Module *m=context;++m->calls;m->pending=1;
  if(m->fail_tx) return 0;
  if(m->module_baud!=m->uart_baud) {
    if(m->noise) strcpy(m->reply,"garbage\r\n");else m->pending=0;
    return 1;
  }
  if(!strcmp(cmd,"AT\r\n")) strcpy(m->reply,"OK\r\n");
  else if(!strcmp(cmd,"AT+RESET\r\n")) strcpy(m->reply,"OK\r\n");
  else if(!strcmp(cmd,"AT+VERSION\r\n")) {
    strcpy(m->reply,m->version_variant==1?"+VERSION=v9.9\r\n":
      m->version_variant==2?"ERROR\r\n":m->version_variant==4?"Firmware custom-2026\r\n":"+VERSION=v6.1\r\n");
    if(m->version_variant==3) m->pending=0;
  }
  else if((m->profile_error==1 && !strcmp(cmd,"AT+ROLE\r\n")) ||
          (m->profile_error==2 && !strcmp(cmd,"AT+NAME\r\n")) ||
          (m->profile_error==3 && !strcmp(cmd,"AT+UUID\r\n")) ||
          (m->profile_error==4 && !strcmp(cmd,"AT+CHAR\r\n"))) strcpy(m->reply,"ERROR\r\n");
  else if(!strcmp(cmd,"AT+ROLE\r\n")) (void)snprintf(m->reply,sizeof(m->reply),"+ROLE=%u\r\n",m->role);
  else if(!strcmp(cmd,"AT+NAME\r\n")) (void)snprintf(m->reply,sizeof(m->reply),"+NAME=%s\r\n",m->name);
  else if(!strcmp(cmd,"AT+UUID\r\n")) (void)snprintf(m->reply,sizeof(m->reply),"+UUID=%s\r\n",m->uuid);
  else if(!strcmp(cmd,"AT+CHAR\r\n")) (void)snprintf(m->reply,sizeof(m->reply),"+CHAR=%s\r\n",m->characteristic);
  else {
    if(!strcmp(cmd,"AT+ROLE0\r\n")) {if(!m->ignore_role) m->role=0;}
    else if(!strncmp(cmd,"AT+NAME",7)) {memcpy(m->name,cmd+7,10);m->name[10]=0;}
    else if(!strcmp(cmd,"AT+UUIDFFE0\r\n")) memcpy(m->uuid,"FFE0",5);
    else if(!strcmp(cmd,"AT+CHARFFE1\r\n")) memcpy(m->characteristic,"FFE1",5);
    else return 0;
    strcpy(m->reply,"OK\r\n");if(m->drop_ack) m->pending=0;
  }
  return 1;
}
static int module_baud(void *context, uint32_t baud)
{
  Module *m=context;++m->baud_calls;m->uart_baud=baud;return !m->fail_baud;
}
static void feed(BtBoot *b, const char *reply, uint32_t now)
{while(*reply) bt_boot_byte(b,(uint8_t)*reply++,now);}
static void simulate(BtBoot *b, Module *m, uint32_t now)
{
  bt_boot_poll(b,now);
  for(unsigned i=0;i<60 && bt_boot_busy(b);++i) {
    if(m->pending) {m->pending=0;feed(b,m->reply,now+1);now+=30;}
    else now+=350;
    bt_boot_poll(b,now);
  }
}
uint32_t bt_boot_selftest(void)
{
  uint32_t e=0;BtBoot b;Module m;const uint8_t name[3]={'A','0','1'};
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);bt_boot_poll(&b,499);
  if(m.calls) e|=1;
  simulate(&b,&m,500);if(b.state!=BT_READY || b.writes || m.calls!=10 || !b.present) e|=2;
  module_init(&m);m.role=1;strcpy(m.name,"OLD");strcpy(m.uuid,"1234");strcpy(m.characteristic,"5678");
  bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.writes!=4 || m.calls!=29 || !b.reset_done || strcmp(m.name,"CLM-413031")) e|=4;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);
  bt_boot_poll(&b,500);m.pending=0;bt_boot_poll(&b,850);m.pending=0;bt_boot_poll(&b,1200);
  if(b.state!=BT_NO_AT || b.present || m.calls!=2) e|=8;
  bt_boot_poll(&b,9000);if(m.calls!=2) e|=16;
  module_init(&m);m.version_variant=1;bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.writes || m.calls!=10) e|=32;
  module_init(&m);m.role=1;m.ignore_role=1;bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_FAILED || b.writes!=1) e|=64;
  module_init(&m);m.role=1;m.drop_ack=1;bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.writes!=1) e|=128;
  module_init(&m);m.fail_tx=1;bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_FAILED || m.calls!=1) e|=256;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);bt_boot_poll(&b,500);
  if(bt_boot_byte(&b,0,501) || b.state!=BT_LINK_ACTIVE || bt_boot_byte(&b,1,502)) e|=512;
  bt_boot_poll(&b,6000);if(m.calls!=1) e|=1024;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);bt_boot_poll(&b,500);
  feed(&b,"NOTOK\r\n",501);bt_boot_poll(&b,530);if(b.present || b.state!=BT_FAILED) e|=2048;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);bt_boot_poll(&b,500);
  for(unsigned i=0;i<70;++i) bt_boot_byte(&b,'X',501);
  if(b.state!=BT_FAILED) e|=4096;
  module_init(&m);bt_boot_init(&b,name,UINT32_MAX-700,module_send,&m);simulate(&b,&m,UINT32_MAX-200);
  if(b.state!=BT_READY) e|=8192;
  module_init(&m);bt_boot_init(&b,(const uint8_t *)"\0\xff\n",0,module_send,&m);
  if(strcmp(b.name,"CLM-00FF0A")) e|=16384;
  bt_boot_poll(&b,6500);if(b.state!=BT_FAILED || m.calls) e|=32768;
  /* New MCU-side baud discovery; never sends a module baud setter. */
  static const uint32_t speeds[]={9600,19200,38400,57600,115200,4800,2400,1200};
  for(unsigned i=0;i<sizeof(speeds)/sizeof(speeds[0]);++i) {
    module_init(&m);m.module_baud=speeds[i];
    bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);
    simulate(&b,&m,500);
    if(b.state!=BT_READY || b.baud!=speeds[i] || m.uart_baud!=speeds[i] || b.writes || m.baud_calls!=i) e|=65536;
  }
  module_init(&m);m.module_baud=57600;m.role=1;m.drop_ack=1;
  bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.baud!=57600 || b.writes!=1 || !b.reset_done) e|=131072;
  module_init(&m);m.module_baud=0;
  bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);simulate(&b,&m,500);
  if(b.state!=BT_NO_AT || b.baud!=9600 || m.uart_baud!=9600 || m.baud_calls!=8 || b.writes) e|=262144;
  module_init(&m);m.module_baud=38400;m.noise=1;
  bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.baud!=38400) e|=524288;
  module_init(&m);m.module_baud=115200;m.version_variant=1;
  bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.baud!=115200 || b.writes) e|=1048576;
  module_init(&m);m.module_baud=19200;m.fail_baud=1;
  bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);simulate(&b,&m,500);
  if(b.state!=BT_FAILED || b.writes) e|=2097152;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);
  bt_boot_poll(&b,500);
  if(bt_boot_byte(&b,0,501) || b.state!=BT_LINK_ACTIVE || m.baud_calls) e|=4194304;
  module_init(&m);m.module_baud=1200;
  bt_boot_init(&b,name,UINT32_MAX-700,module_send,&m);bt_boot_set_uart(&b,module_baud);
  simulate(&b,&m,UINT32_MAX-200);
  if(b.state!=BT_READY || b.baud!=1200) e|=8388608;
  module_init(&m);m.module_baud=19200;
  bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);
  bt_boot_poll(&b,500);bt_boot_poll(&b,850);bt_boot_poll(&b,1200);
  if(b.state!=BT_UART_WAIT || !bt_boot_byte(&b,0,1201) || b.state!=BT_UART_WAIT) e|=16777216;
  simulate(&b,&m,1250);
  if(b.state!=BT_READY || b.baud!=19200) e|=16777216;
  unsigned calls=m.calls,changes=m.baud_calls;
  bt_boot_poll(&b,20000);
  if(m.calls!=calls || m.baud_calls!=changes) e|=33554432;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);bt_boot_set_uart(&b,module_baud);
  bt_boot_poll(&b,13000);
  if(b.state!=BT_FAILED || m.calls || m.baud_calls) e|=67108864;
  for(unsigned version=0;version<=4;++version) {
    module_init(&m);m.version_variant=version;m.role=1;strcpy(m.name,"OLD");
    bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
    if(b.state!=BT_READY || b.writes!=2 || !b.profile_checked || strcmp(m.name,"CLM-413031")) e|=134217728;
  }
  for(unsigned capability=1;capability<=4;++capability) {
    module_init(&m);m.version_variant=1;m.role=1;m.profile_error=capability;
    bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
    if(b.state!=BT_UNSUPPORTED || b.writes || b.profile_checked || m.role!=1) e|=268435456;
  }
  module_init(&m);m.role=1;strcpy(m.uuid,"BAD!");
  bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_UNSUPPORTED || b.writes) e|=536870912;
  module_init(&m);m.role=1;m.name[0]=0;
  bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_UNSUPPORTED || b.writes) e|=1073741824;
  return e;
}
