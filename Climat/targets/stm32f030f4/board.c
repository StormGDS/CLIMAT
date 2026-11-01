/* F030F4 approved pin-map adapter: CMSIS registers only, no F7 HAL/DWT/FPU. */
#include "stm32f030x6.h" /* ST uses this header for x4 and x6 devices. */
#include "board_config.h"
#include "service_console.h"
#include "bt_boot.h"
#include <string.h>
uint32_t SystemCoreClock=48000000U;
static volatile uint32_t ticks;
static TemperatureProbe probe;
static BtBoot bluetooth;
extern uint32_t _stack_limit,_estack;
extern uint8_t __config_start__,__config_end__;
#define CONFIG_ADDRESS 0x08003C00U
#define CANARY 0xA55AC33CU
static void watchdog(void) {IWDG->KR=0xAAAA;}
static void fatal(void) {for(;;) __NOP();} /* IWDG resets; never pet on failure. */
void SysTick_Handler(void) {++ticks;}
static uint32_t now_ms(void) {return ticks;}
static void delay_us(unsigned us)
{uint16_t start=(uint16_t)TIM14->CNT;while((uint16_t)((uint16_t)TIM14->CNT-start)<us) __NOP();}
static void low(void) {ONEWIRE_GPIO->BSRR=1U<<(ONEWIRE_PIN+16);}
static void release(void) {ONEWIRE_GPIO->BSRR=1U<<ONEWIRE_PIN;}
static unsigned level(void) {return (ONEWIRE_GPIO->IDR>>ONEWIRE_PIN)&1U;}
static int ow_reset(void)
{
  release();delay_us(5);if(!level()) return -1;
  low();delay_us(480);uint32_t irq=__get_PRIMASK();__disable_irq();
  release();delay_us(70);unsigned present=!level();__set_PRIMASK(irq);
  delay_us(410);if(!level()) return -1;return (int)present;
}
static void ow_write(unsigned bit)
{
  uint32_t irq=__get_PRIMASK();__disable_irq();low();delay_us(bit?6:60);release();
  __set_PRIMASK(irq);delay_us(bit?64:10);
}
static unsigned ow_read(void)
{
  uint32_t irq=__get_PRIMASK();__disable_irq();low();delay_us(3);release();delay_us(9);
  unsigned result=level();__set_PRIMASK(irq);delay_us(58);return result;
}
static int uart_send(const uint8_t *p, unsigned n)
{
  uint32_t start=ticks;
  while(n--) {
    while(!(USART1->ISR&USART_ISR_TXE)) if((uint32_t)(ticks-start)>150) {service_phone_loss();return 0;}
    USART1->TDR=*p++;
  }
  return 1;
}
static void uart_write(const uint8_t *p, unsigned n) {(void)uart_send(p,n);}
static int bt_send(void *context, const char *p)
{(void)context;return uart_send((const uint8_t *)p,(unsigned)strlen(p));}
static int bt_filter(uint8_t byte, uint32_t now) {return bt_boot_byte(&bluetooth,byte,now);}
void USART1_IRQHandler(void)
{
  uint32_t status=USART1->ISR;
  if(status&(USART_ISR_ORE|USART_ISR_FE|USART_ISR_NE|USART_ISR_PE)) {
    USART1->ICR=USART_ICR_ORECF|USART_ICR_FECF|USART_ICR_NCF|USART_ICR_PECF;
    if(status&USART_ISR_RXNE) (void)USART1->RDR;
    service_phone_loss();
  } else if(status&USART_ISR_RXNE) {
    uint8_t b=(uint8_t)USART1->RDR;
    service_phone_byte(b);
  }
}
/* Logical bits 0..3 map to PA1..PA4. Reset OFF before setting ON. */
static uint8_t relay_apply(uint8_t mask)
{
  uint32_t pins=((uint32_t)mask<<1)&RELAY_PIN_MASK;
  RELAY_GPIO->BSRR=(RELAY_PIN_MASK & ~pins)<<16;
  RELAY_GPIO->BSRR=pins;
  return (uint8_t)((RELAY_GPIO->ODR>>1)&15U);
}
static int flash_read(uint8_t *b)
{
  if((uintptr_t)&__config_start__!=CONFIG_ADDRESS || (uintptr_t)&__config_end__!=0x08004000U) return 0;
  memcpy(b,(const void *)CONFIG_ADDRESS,SETTINGS_SIZE);return 1;
}
/* Entire busy polling loop is in RAM. The microsecond timer is accumulated
 * across wraps; no assumption that page erase fits one 16-bit timer period. */
__attribute__((section(".ramfunc"),noinline))
static int flash_program(const uint8_t *record, uint32_t *elapsed)
{
  uint16_t previous=(uint16_t)TIM14->CNT;
  FLASH->SR=FLASH_SR_PGERR|FLASH_SR_WRPRTERR|FLASH_SR_EOP;
  FLASH->CR=FLASH_CR_PER;FLASH->AR=CONFIG_ADDRESS;FLASH->CR|=FLASH_CR_STRT;
  for(unsigned op=0;op<=SETTINGS_SIZE/2;++op) {
    while(FLASH->SR&FLASH_SR_BSY) {
      uint16_t current=(uint16_t)TIM14->CNT;*elapsed+=(uint16_t)(current-previous);previous=current;
      if(*elapsed>200000U) for(;;) __NOP(); /* watchdog handles stuck flash */
    }
    if(FLASH->SR&(FLASH_SR_PGERR|FLASH_SR_WRPRTERR)) {FLASH->CR=FLASH_CR_LOCK;return 0;}
    if(op==SETTINGS_SIZE/2) break;
    /* Payload first, CRC included, then two halfwords of magic. A partial
     * magic or payload is rejected by the shared decoder/CRC at next boot. */
    unsigned offset=op<SETTINGS_SIZE/2-2?4+2*op:2*(op-(SETTINGS_SIZE/2-2));
    FLASH->CR=FLASH_CR_PG;
    *(volatile uint16_t *)(CONFIG_ADDRESS+offset)=(uint16_t)(record[offset]|((uint16_t)record[offset+1]<<8));
  }
  uint16_t current=(uint16_t)TIM14->CNT;*elapsed+=(uint16_t)(current-previous);
  FLASH->CR=FLASH_CR_LOCK;return 1;
}
static int flash_write(const uint8_t *b)
{
  Settings checked;if(settings_decode(b,&checked)!=SETTINGS_OK) return 0;
  if((uintptr_t)&__config_start__!=CONFIG_ADDRESS) return 0;
  watchdog();uint32_t irq=__get_PRIMASK();__disable_irq();
  FLASH->KEYR=0x45670123U;FLASH->KEYR=0xCDEF89ABU;
  if(FLASH->CR&FLASH_CR_LOCK) {__set_PRIMASK(irq);return 0;}
  uint32_t elapsed=0;int result=flash_program(b,&elapsed);ticks+=elapsed/1000U;
  USART1->ICR=USART_ICR_ORECF|USART_ICR_FECF|USART_ICR_NCF|USART_ICR_PECF;
  if(USART1->ISR&USART_ISR_RXNE) (void)USART1->RDR;
  service_phone_loss();__set_PRIMASK(irq);watchdog();
  return result && !memcmp(b,(const void *)CONFIG_ADDRESS,SETTINGS_SIZE);
}
static void hardware_init(void)
{
  RCC->CSR|=RCC_CSR_LSION;while(!(RCC->CSR&RCC_CSR_LSIRDY)) __NOP();
  IWDG->KR=0x5555;IWDG->PR=4;IWDG->RLR=1999;while(IWDG->SR) __NOP();IWDG->KR=0xCCCC;watchdog();
  RCC->CR|=RCC_CR_HSION;while(!(RCC->CR&RCC_CR_HSIRDY)) __NOP();
  FLASH->ACR=FLASH_ACR_PRFTBE|FLASH_ACR_LATENCY;
  RCC->CFGR=RCC_CFGR_PLLMUL12;RCC->CR|=RCC_CR_PLLON;
  while(!(RCC->CR&RCC_CR_PLLRDY)) {__NOP();}
  RCC->CFGR|=RCC_CFGR_SW_PLL;
  while((RCC->CFGR&RCC_CFGR_SWS)!=RCC_CFGR_SWS_PLL) __NOP();
  RCC->AHBENR|=RCC_AHBENR_GPIOAEN;
  RCC->APB1ENR|=RCC_APB1ENR_TIM14EN;TIM14->PSC=47;TIM14->ARR=65535;TIM14->EGR=TIM_EGR_UG;TIM14->CR1=TIM_CR1_CEN;
  RELAY_GPIO->BSRR=RELAY_PIN_MASK<<16;
  for(unsigned pin=0;pin<16;++pin) if(RELAY_PIN_MASK&(1U<<pin))
    RELAY_GPIO->MODER=(RELAY_GPIO->MODER&~(3U<<(2*pin)))|(1U<<(2*pin));
  release();ONEWIRE_GPIO->OTYPER|=1U<<ONEWIRE_PIN;
  ONEWIRE_GPIO->MODER=(ONEWIRE_GPIO->MODER&~(3U<<(2*ONEWIRE_PIN)))|(1U<<(2*ONEWIRE_PIN));
  GPIOA->MODER=(GPIOA->MODER&~((3U<<18)|(3U<<20)))|(2U<<18)|(2U<<20);
  GPIOA->AFR[1]=(GPIOA->AFR[1]&~((15U<<4)|(15U<<8)))|(1U<<4)|(1U<<8);
  GPIOA->PUPDR=(GPIOA->PUPDR&~(3U<<20))|(1U<<20);
  RCC->APB2ENR|=RCC_APB2ENR_USART1EN;USART1->BRR=48000000U/9600U;
  USART1->CR3=USART_CR3_EIE;USART1->CR1=USART_CR1_UE|USART_CR1_TE|USART_CR1_RE|USART_CR1_RXNEIE|USART_CR1_PEIE;
  NVIC_SetPriority(USART1_IRQn,1);NVIC_EnableIRQ(USART1_IRQn);
  SysTick_Config(48000);NVIC_SetPriority(SysTick_IRQn,3);
}
int main(void)
{
  /* Respect the actual startup frame, including LTO inlining. The untouched
   * active frame and a 32-byte margin are conservatively counted as used. */
  volatile uint32_t *guard=(volatile uint32_t *)((__get_MSP()-32U)&~3U);
  if(guard<=&_stack_limit) fatal();
  for(volatile uint32_t *p=&_stack_limit;p<guard;++p) *p=CANARY;
  *guard=0;
  hardware_init();ProbeBus bus={ow_reset,ow_write,ow_read,now_ms};probe_init(&probe,bus);
  SettingsBackend storage={flash_read,flash_write};service_init(&probe,0,0,storage);
  service_set_relay_driver(relay_apply);
  service_phone_init(uart_write);
  {
    uint8_t name[3];service_device_name(name);bt_boot_init(&bluetooth,name,ticks,bt_send,0);service_set_phone_filter(bt_filter);
  }
  uint32_t fed=ticks;
  for(;;) {
    service_poll(ticks);service_phone_poll(ticks);bt_boot_poll(&bluetooth,ticks);
    if(probe_poll(&probe,ticks)) service_sample(ticks);
    if(*(volatile uint32_t *)&_stack_limit!=CANARY) fatal();
    if((uint32_t)(ticks-fed)>=100) {watchdog();fed=ticks;}
  }
}
