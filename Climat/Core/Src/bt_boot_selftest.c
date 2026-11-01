#include "bt_boot.h"
#include <string.h>
#include <stdio.h>
typedef struct {
  char reply[64],name[11],uuid[5],characteristic[5];
  unsigned role,calls,pending,drop_ack,ignore_role,fail_tx,unsupported;
} Module;
static void module_init(Module *m)
{
  memset(m,0,sizeof(*m));memcpy(m->name,"CLM-413031",11);
  memcpy(m->uuid,"FFE0",5);memcpy(m->characteristic,"FFE1",5);
}
static int module_send(void *context, const char *cmd)
{
  Module *m=context;++m->calls;m->pending=1;
  if(m->fail_tx) return 0;
  if(!strcmp(cmd,"AT\r\n")) strcpy(m->reply,"OK\r\n");
  else if(!strcmp(cmd,"AT+RESET\r\n")) strcpy(m->reply,"OK\r\n");
  else if(!strcmp(cmd,"AT+VERSION\r\n")) strcpy(m->reply,m->unsupported?"+VERSION=v9.9\r\n":"+VERSION=v6.1\r\n");
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
  simulate(&b,&m,500);if(b.state!=BT_READY || b.writes || m.calls!=6 || !b.present) e|=2;
  module_init(&m);m.role=1;strcpy(m.name,"OLD");strcpy(m.uuid,"1234");strcpy(m.characteristic,"5678");
  bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_READY || b.writes!=4 || m.calls!=25 || !b.reset_done || strcmp(m.name,"CLM-413031")) e|=4;
  module_init(&m);bt_boot_init(&b,name,0,module_send,&m);
  bt_boot_poll(&b,500);m.pending=0;bt_boot_poll(&b,850);m.pending=0;bt_boot_poll(&b,1200);
  if(b.state!=BT_NO_AT || b.present || m.calls!=2) e|=8;
  bt_boot_poll(&b,9000);if(m.calls!=2) e|=16;
  module_init(&m);m.unsupported=1;bt_boot_init(&b,name,0,module_send,&m);simulate(&b,&m,500);
  if(b.state!=BT_UNSUPPORTED || b.writes || m.calls!=2) e|=32;
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
  return e;
}
