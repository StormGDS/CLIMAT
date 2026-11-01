#include <stdint.h>
extern uint32_t _estack,_sidata,_sdata,_edata,_sbss,_ebss;
extern int main(void);
void Reset_Handler(void);
void SysTick_Handler(void);
void USART1_IRQHandler(void);
static void fault(void) {for(;;) {__asm volatile("nop");}}
typedef void (*Isr)(void);
#define D fault
__attribute__((section(".isr_vector"),used))
const Isr vectors[48]={
  (Isr)&_estack,Reset_Handler,D,D,0,0,0,0,0,0,0,D,0,0,D,SysTick_Handler,
  D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,
  D,D,D,D,D,D,D,D,D,D,D,USART1_IRQHandler,D,D,D,D
};
void Reset_Handler(void)
{
  volatile uint32_t *to=&_sdata;const uint32_t *from=&_sidata;
  while(to<&_edata) *to++=*from++;
  to=&_sbss;while(to<&_ebss) *to++=0;
  (void)main();fault();
}
