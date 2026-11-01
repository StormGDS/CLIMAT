/* NUCLEO-F767ZI adapter only. No Cortex-M7 dependencies in temperature_probe. */
#include "main.h"
#include "temperature_probe.h"
#include "sensor_stage1.h"
#include "service_console.h"
#include "settings_flash.h"
#include "bt_boot.h"
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart6;
extern IWDG_HandleTypeDef hiwdg;
static TemperatureProbe probe;
static uint32_t cycles_per_us, last_tick, last_refresh;
static uint8_t rx_byte, phone_byte;
static volatile uint8_t rx_restart, phone_restart;
static BtBoot bluetooth;
static uint8_t bluetooth_reported;

/* User confirmed on 2026-10-28: connected drivers are active HIGH.
 * Reset/CubeMX initialize all four outputs LOW. Only arbiter masks reach here.
 * Clear every outgoing relay before setting any incoming relay, across ports. */
static uint8_t relay_write(uint8_t mask)
{
  uint32_t heaters=((mask&1U)?K1_HEATER1_Pin:0U)|((mask&2U)?K2_HEATER2_Pin:0U);
  uint32_t fans=((mask&4U)?K3_FAN1_Pin:0U)|((mask&8U)?K4_FAN2_Pin:0U);
  GPIOF->BSRR=((K1_HEATER1_Pin|K2_HEATER2_Pin)&~heaters)<<16;
  GPIOE->BSRR=((K3_FAN1_Pin|K4_FAN2_Pin)&~fans)<<16;
  __DSB();
  GPIOF->BSRR=heaters;GPIOE->BSRR=fans;__DSB();
  return (uint8_t)(((GPIOF->ODR&K1_HEATER1_Pin)?1U:0U)|
                   ((GPIOF->ODR&K2_HEATER2_Pin)?2U:0U)|
                   ((GPIOE->ODR&K3_FAN1_Pin)?4U:0U)|
                   ((GPIOE->ODR&K4_FAN2_Pin)?8U:0U));
}

static void delay_us(uint32_t us)
{
  uint32_t start=DWT->CYCCNT;
  uint32_t ticks=us*cycles_per_us;
  while((uint32_t)(DWT->CYCCNT-start)<ticks) { __NOP(); }
}
static void low(void) { ONEWIRE_GPIO_Port->BSRR=(uint32_t)ONEWIRE_Pin<<16; }
static void release(void) { ONEWIRE_GPIO_Port->BSRR=ONEWIRE_Pin; }
static unsigned level(void) { return (ONEWIRE_GPIO_Port->IDR & ONEWIRE_Pin)!=0; }
static int reset(void)
{
  release(); delay_us(5);
  if(!level()) return -1;
  /* Reset low may safely stretch under interrupts. Keep UART responsive. */
  low(); delay_us(480);
  uint32_t mask=__get_PRIMASK(); __disable_irq();
  release(); delay_us(70);
  unsigned present=!level();
  __set_PRIMASK(mask);
  delay_us(410);
  if(!level()) return -1;
  return (int)present;
}
static void write_bit(unsigned bit)
{
  uint32_t mask=__get_PRIMASK(); __disable_irq();
  low(); delay_us(bit ? 6 : 60); release();
  __set_PRIMASK(mask);
  delay_us(bit ? 64 : 10);
}
static unsigned read_bit(void)
{
  uint32_t mask=__get_PRIMASK(); __disable_irq();
  low(); delay_us(3); release(); delay_us(9);
  unsigned v=level();
  __set_PRIMASK(mask);
  delay_us(58);
  return v;
}
static void print(const char *s)
{
  /* Bounded TX, no IRQ/DMA buffer ownership, no float printf or heap. */
  (void)HAL_UART_Transmit(&huart3,(uint8_t *)s,(uint16_t)strlen(s),100);
}
static void phone_arm(void)
{
  __HAL_UART_CLEAR_OREFLAG(&huart6);
  if(HAL_UART_Receive_IT(&huart6,&phone_byte,1)!=HAL_OK) phone_restart=1;
}
static void phone_transmit(const uint8_t *p, unsigned n)
{
  /* At 9600 baud a maximum frame takes <80ms; RX IRQs stay active. */
  if(HAL_UART_Transmit(&huart6,(uint8_t *)p,(uint16_t)n,(n*10000U)/(bluetooth.baud?bluetooth.baud:9600U)+30U)!=HAL_OK) service_phone_loss();
}
static int bluetooth_send(void *context, const char *command)
{
  (void)context;
  return HAL_UART_Transmit(&huart6,(uint8_t *)command,(uint16_t)strlen(command),(strlen(command)*10000U)/(bluetooth.baud?bluetooth.baud:9600U)+30U)==HAL_OK;
}
static int bluetooth_baud(void *context, uint32_t baud)
{
  (void)context;(void)HAL_UART_AbortReceive(&huart6);service_phone_loss();
  huart6.Init.BaudRate=baud;
  if(HAL_UART_Init(&huart6)!=HAL_OK) return 0;
  __HAL_UART_CLEAR_OREFLAG(&huart6);
  __HAL_UART_SEND_REQ(&huart6,UART_RXDATA_FLUSH_REQUEST);
  phone_arm();return 1;
}
static int bluetooth_filter(uint8_t byte, uint32_t now)
{return bt_boot_byte(&bluetooth,byte,now);}
/* AT remains an explicit local bring-up command, outside the binary link. */
static int bluetooth_command(const char *command)
{
  char text[160];
  if(!strcmp(command,"BT STATUS")) {
    (void)snprintf(text,sizeof(text),"BT USART6 baud=%lu TX=PG14 RX=PG9 rx_level=%u\r\n",
      (unsigned long)huart6.Init.BaudRate,(unsigned)((GPIOG->IDR>>9)&1U));
    print(text);
    (void)snprintf(text,sizeof(text),"BT BOOT state=%s present_by_at=%u writes=%u desired_name=%s phase=%u\r\n",
      bt_boot_status(&bluetooth),bluetooth.present,bluetooth.writes,bluetooth.name,bluetooth.phase);
    print(text);return 1;
  }
  if(bt_boot_busy(&bluetooth)) {print("ERR BT_STARTUP_IN_PROGRESS\r\n");return 1;}
  unsigned crlf=!strncmp(command,"BT ATLF ",8);
  unsigned version=!strcmp(command,"BT VERSION");
  unsigned vers=!strcmp(command,"BT VERS");
  unsigned help=!strcmp(command,"BT HELP");
  const char *query=0;
  static const char *const queries[]={"ROLE","NAME","STATE","PWRM","IMME","UUID","CHAR","BAUD","LADDR"};
  if(!strncmp(command,"BT QUERY ",9)) {
    for(unsigned i=0;i<sizeof(queries)/sizeof(queries[0]);++i)
      if(!strcmp(command+9,queries[i])) {query=queries[i];break;}
    if(!query) return 0;
  }
  if(!crlf && !version && !vers && !help && !query && strncmp(command,"BT AT ",6)) return 0;
  static const char *const speeds[]={"9600","115200","19200","38400","57600","4800","2400","230400"};
  static const uint32_t bauds[]={9600,115200,19200,38400,57600,4800,2400,230400};
  unsigned index;
  if(version || vers || help || query) index=0;
  else for(index=0;index<sizeof(bauds)/sizeof(bauds[0]);++index) if(!strcmp(command+(crlf?8:6),speeds[index])) break;
  if(index==sizeof(bauds)/sizeof(bauds[0])) return 0;
  (void)HAL_UART_AbortReceive(&huart6);service_phone_loss();
  uint32_t old_baud=huart6.Init.BaudRate;huart6.Init.BaudRate=bauds[index];
  if(HAL_UART_Init(&huart6)!=HAL_OK) {
    huart6.Init.BaudRate=old_baud;(void)HAL_UART_Init(&huart6);phone_arm();
    print("ERR BT UART_INIT\r\n");return 1;
  }
  __HAL_UART_CLEAR_OREFLAG(&huart6);__HAL_UART_SEND_REQ(&huart6,UART_RXDATA_FLUSH_REQUEST);
  char query_command[24];if(query) (void)snprintf(query_command,sizeof(query_command),"AT+%s\r\n",query);
  const char *request=query?query_command:version?"AT+VERSION\r\n":vers?"AT+VERS?\r\n":help?"AT+HELP\r\n":crlf?"AT\r\n":"AT";
  if(HAL_UART_Transmit(&huart6,(uint8_t *)request,(uint16_t)strlen(request),50)!=HAL_OK) {phone_arm();print("ERR BT TX\r\n");return 1;}
  if(help) {
    /* Stream the module's own command reference without a large RAM buffer.
     * Local maintenance only; automatic boot never runs HELP. */
    uint32_t start=HAL_GetTick(),last=start;unsigned total=0;
    print("BT HELP BEGIN\r\n");
    while((uint32_t)(HAL_GetTick()-start)<3500 && (!total || (uint32_t)(HAL_GetTick()-last)<100)) {
      uint8_t b;
      if(HAL_UART_Receive(&huart6,&b,1,10)==HAL_OK) {
        char one[2]={(char)(b==13 || b==10 || b==9 || (b>=32 && b<=126)?b:'.'),0};
        print(one);++total;last=HAL_GetTick();
      }
      (void)HAL_IWDG_Refresh(&hiwdg);
    }
    print("\r\nBT HELP END\r\n");phone_arm();return 1;
  }
  uint8_t bytes[128];unsigned count=0;uint32_t start=HAL_GetTick(),errors=0;
  while(count<sizeof(bytes) && (uint32_t)(HAL_GetTick()-start)<300) {
    HAL_StatusTypeDef result=HAL_UART_Receive(&huart6,bytes+count,1,10);
    if(result==HAL_OK) ++count;
    else if(result==HAL_ERROR) {errors|=HAL_UART_GetError(&huart6);__HAL_UART_CLEAR_OREFLAG(&huart6);}
  }
  (void)snprintf(text,sizeof(text),"BT AT baud=%lu count=%u errors=0x%lX hex=",
    (unsigned long)bauds[index],count,(unsigned long)errors);print(text);
  for(unsigned i=0;i<count;++i) { (void)snprintf(text,sizeof(text),"%02X",bytes[i]);print(text); }
  print(" text=");
  for(unsigned i=0;i<count;++i) text[i]=bytes[i]>=32 && bytes[i]<=126?(char)bytes[i]:'.';
  text[count]=0;print(text);print(count?"\r\n":"NO_RESPONSE\r\n");
  phone_arm();
  return 1;
}
static void fixed(char *dst, size_t n, int32_t value, unsigned places)
{
  uint32_t scale=places==4 ? 10000U : 1000U;
  uint32_t magnitude=(uint32_t)(value<0 ? -value : value);
  (void)snprintf(dst,n,"%s%lu.%0*lu",value<0 ? "-" : "",
    (unsigned long)(magnitude/scale),(int)places,(unsigned long)(magnitude%scale));
}
static void report(uint32_t now)
{
  char line[192], temperature[20], rate[20];
  (void)snprintf(line,sizeof(line),"t=%lu ms scan=%s extra_or_limit=%u\r\n",
    (unsigned long)now,probe.scan_status,probe.scan_limited); print(line);
  for(unsigned i=0;i<2;++i) {
    ProbeSensor *s=&probe.sensor[i]; char rom[17];
    for(unsigned j=0;j<8;++j) (void)snprintf(rom+2*j,3,"%02X",s->rom[j]);
    if(s->valid) fixed(temperature,sizeof(temperature),(int32_t)s->raw16*625,4);
    else strcpy(temperature,"NA");
    if(s->trend_valid) {
      fixed(rate,sizeof(rate),s->rate_milli_per_min,3);
    } else { strcpy(rate,"WARMUP"); }
    (void)snprintf(line,sizeof(line),
      "S%u ROM=%s T=%s C status=%s errors=%lu dT=%s C/min\r\n",
      i+1,s->assigned ? rom : "UNASSIGNED",temperature,s->status,
      (unsigned long)s->errors,rate); print(line);
  }
}
static int store_settings(const uint8_t data[SETTINGS_SIZE])
{
  (void)HAL_UART_AbortReceive(&huart3);
  (void)HAL_UART_AbortReceive(&huart6);
  int ok=settings_flash_write(data);
  __HAL_UART_CLEAR_OREFLAG(&huart3);
  if(HAL_UART_Receive_IT(&huart3,&rx_byte,1)!=HAL_OK) rx_restart=1;
  service_phone_loss();phone_arm();
  return ok;
}
void sensor_stage1_init(void)
{
  /* Enforce OD also if a future CubeMX regeneration changes its GPIO block. */
  GPIO_InitTypeDef gpio={0};
  release(); gpio.Pin=ONEWIRE_Pin; gpio.Mode=GPIO_MODE_OUTPUT_OD;
  gpio.Pull=GPIO_NOPULL; gpio.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ONEWIRE_GPIO_Port,&gpio);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  /* Cortex-M7 DWT can be software-locked after a standalone reset. */
  DWT->LAR=0xc5acce55U;
  __DSB(); __ISB();
  DWT->CYCCNT=0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  __DSB(); __ISB();
  uint32_t before=DWT->CYCCNT;
  for(volatile unsigned i=0;i<64;++i) { __NOP(); }
  if(DWT->CYCCNT==before) {
    print("ERROR: DWT counter stopped; 1-Wire test cannot start.\r\n");
    Error_Handler();
  }
  cycles_per_us=SystemCoreClock/1000000U;
  ProbeBus bus={reset,write_bit,read_bit,HAL_GetTick}; probe_init(&probe,bus);
  SettingsBackend storage={settings_flash_read,store_settings};
  service_init(&probe,report,print,storage);
  service_set_relay_driver(relay_write);
  service_set_bt_handler(bluetooth_command);
  service_phone_init(phone_transmit);phone_arm();
  uint8_t name[3];service_device_name(name);
  bt_boot_init(&bluetooth,name,HAL_GetTick(),bluetooth_send,0);
  bt_boot_set_uart(&bluetooth,bluetooth_baud);
  service_set_phone_filter(bluetooth_filter);
  if(HAL_UART_Receive_IT(&huart3,&rx_byte,1)!=HAL_OK) Error_Handler();
  last_tick=last_refresh=HAL_GetTick();
  print("\r\nCLIMAT PHONE PROTOCOL v2 AUTO: USART3 115200 8N1\r\n"
        "Physical relay control ENABLED, active HIGH. External 3V3 DS18B20 VDD required.\r\n"
        "S1/S2 use explicit ROM bindings; SAVE persists roles.\r\n"
        "Period=1s; raw T; endpoint trend window=5s; no prediction or filtering.\r\n");
}
void sensor_stage1_poll(void)
{
  uint32_t now=HAL_GetTick();
  if(rx_restart) {
    rx_restart=0;
    (void)HAL_UART_AbortReceive(&huart3);
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    if(HAL_UART_Receive_IT(&huart3,&rx_byte,1)!=HAL_OK) rx_restart=1;
  }
  service_poll(now);
  if(phone_restart) {
    phone_restart=0;(void)HAL_UART_AbortReceive(&huart6);service_phone_loss();phone_arm();
  }
  service_phone_poll(HAL_GetTick());
  bt_boot_poll(&bluetooth,HAL_GetTick());
  if(!bluetooth_reported && !bt_boot_busy(&bluetooth)) {
    char text[144];bluetooth_reported=1;
    (void)snprintf(text,sizeof(text),"BT BOOT state=%s present_by_at=%u writes=%u desired_name=%s\r\n",
      bt_boot_status(&bluetooth),bluetooth.present,bluetooth.writes,bluetooth.name);print(text);
    if(bluetooth.state==BT_FAILED || bluetooth.state==BT_UNSUPPORTED) {
      (void)snprintf(text,sizeof(text),"BT failed_phase=%u last_reply=%s\r\n",bluetooth.phase,bluetooth.response);print(text);
    }
  }
  now=HAL_GetTick(); /* SAVE may have erased/programmed Flash. */
  int ready=probe_poll(&probe,now);
  if(ready) {
    service_sample(HAL_GetTick());
    if(service_stream_enabled()) report(HAL_GetTick());
  }
  /* Health = returned from bounded probe/report work AND advancing SysTick.
   * No sensors is a reportable result, not a reason to reset the MCU. */
  now=HAL_GetTick();
  if(now!=last_tick && (uint32_t)(now-last_refresh)>=100) {
    (void)HAL_IWDG_Refresh(&hiwdg); last_refresh=now;
  }
  last_tick=now;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if(uart==&huart6) {service_phone_byte(phone_byte);phone_arm();return;}
  if(uart!=&huart3) return;
  service_rx_byte(rx_byte);
  if(HAL_UART_Receive_IT(uart,&rx_byte,1)!=HAL_OK) {service_rx_error();rx_restart=1;}
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if(uart==&huart3) {service_rx_error();rx_restart=1;}
  if(uart==&huart6) {service_phone_loss();phone_restart=1;}
}
