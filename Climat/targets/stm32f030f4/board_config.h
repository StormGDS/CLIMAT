#ifndef F030_BOARD_CONFIG_H
#define F030_BOARD_CONFIG_H
/* PIN MAP APPROVED BY USER 2026-11-02.
 * K1=PA1 heater 1, K2=PA2 heater 2, K3=PA3 fan 1, K4=PA4 fan 2.
 * TSSOP20: PA0=6, PA1=7, PA2=8, PA3=9, PA4=10, PA5=11,
 * PA9=17 (USART1_TX AF1), PA10=18 (RX AF1), SWD PA13/PA14=19/20.
 * PA5 unused; no service-console selection.
 * All relays active HIGH; initialize LOW (OFF). */
#define ONEWIRE_GPIO GPIOA
#define ONEWIRE_PIN 0U
#define RELAY_GPIO GPIOA
#define RELAY_PIN_MASK ((1U<<1)|(1U<<2)|(1U<<3)|(1U<<4))
#define F030_STACK_BYTES 2048U
#endif
