#ifndef USER_LED_H
#define USER_LED_H

#include "stm32f10x.h"

/* 默认 PB5 低电平点亮；如果开发板相反，将此值改成 0。 */
#define LED_ACTIVE_LOW 1
void Led_Init(void);
void Led_Set(uint8_t on);

#endif
