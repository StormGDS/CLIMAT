/* F767-only adapter. Portable settings.c does not know flash geometry. */
#include "settings_flash.h"
#include "main.h"
#include <string.h>
#define SETTINGS_ADDRESS 0x08010000U
extern IWDG_HandleTypeDef hiwdg;
extern uint8_t __config_start__, __config_end__;
static int geometry_ok(void)
{
  return (uintptr_t)&__config_start__==SETTINGS_ADDRESS &&
    (uintptr_t)&__config_end__==0x08020000U &&
    !(SYSCFG->MEMRMP & SYSCFG_MEMRMP_SWP_FB);
}
static void invalidate(void)
{
  __DSB();
  if(SCB->CCR & SCB_CCR_DC_Msk)
    SCB_InvalidateDCache_by_Addr((uint32_t *)SETTINGS_ADDRESS,(SETTINGS_SIZE+31U)&~31U);
  __DSB();__ISB();
}
int settings_flash_read(uint8_t *b)
{
  if(!geometry_ok()) return 0;
  invalidate();memcpy(b,(const void *)SETTINGS_ADDRESS,SETTINGS_SIZE);return 1;
}
int settings_flash_write(const uint8_t *b)
{
  if(!geometry_ok()) return 0;
  Settings checked;
  if(settings_decode(b,&checked)!=SETTINGS_OK) return 0;
  FLASH_EraseInitTypeDef erase={0}; uint32_t sector_error=0;
  erase.TypeErase=FLASH_TYPEERASE_SECTORS;
  erase.Sector=(FLASH->OPTCR & FLASH_OPTCR_nDBANK)?FLASH_SECTOR_2:FLASH_SECTOR_4;
  erase.NbSectors=1;erase.VoltageRange=FLASH_VOLTAGE_RANGE_3; /* Board 3.3 V */
  (void)HAL_IWDG_Refresh(&hiwdg);
  if(HAL_FLASH_Unlock()!=HAL_OK) return 0;
  uint32_t start_tick=HAL_GetTick(), start_cycles=DWT->CYCCNT;
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_ALL_ERRORS);
  int ok=HAL_FLASHEx_Erase(&erase,&sector_error)==HAL_OK;
  /* Header magic is the commit word, written only after payload and CRC. */
  for(unsigned i=4;ok && i<SETTINGS_SIZE;i+=4) {
    uint32_t word=(uint32_t)b[i] | ((uint32_t)b[i+1]<<8) |
                  ((uint32_t)b[i+2]<<16) | ((uint32_t)b[i+3]<<24);
    ok=HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,SETTINGS_ADDRESS+i,word)==HAL_OK;
  }
  if(ok) {
    uint32_t magic=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
    ok=HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,SETTINGS_ADDRESS,magic)==HAL_OK;
  }
  if(HAL_FLASH_Lock()!=HAL_OK) ok=0;
  /* Same-bank erase stalls code fetch/SysTick. Account for missed ticks using
   * the running DWT (already checked at board startup); sector erase < wrap. */
  uint32_t elapsed=(uint32_t)(DWT->CYCCNT-start_cycles)/(SystemCoreClock/1000U);
  uint32_t ticks=HAL_GetTick()-start_tick;
  if(elapsed>ticks) {uint32_t irq=__get_PRIMASK();__disable_irq();uwTick+=elapsed-ticks;__set_PRIMASK(irq);}
  (void)HAL_IWDG_Refresh(&hiwdg);
  invalidate();
  return ok && !memcmp(b,(const void *)SETTINGS_ADDRESS,SETTINGS_SIZE);
}
