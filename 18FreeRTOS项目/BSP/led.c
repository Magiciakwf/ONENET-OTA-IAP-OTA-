#include "led.h"
#include "main.h"


void Led_Init(void)
{
	GPIO_InitTypeDef Led_InitTypeDef;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	Led_Set(0); /* 先写灭灯电平，再切换为输出，避免上电闪一下。 */
	Led_InitTypeDef.GPIO_Mode = GPIO_Mode_Out_PP;
	Led_InitTypeDef.GPIO_Pin = GPIO_Pin_5;
	Led_InitTypeDef.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(GPIOB,&Led_InitTypeDef);
}

void Led_Set(uint8_t on)
{
    if((on != 0) == (LED_ACTIVE_LOW != 0)) GPIO_ResetBits(GPIOB, GPIO_Pin_5);
    else GPIO_SetBits(GPIOB, GPIO_Pin_5);
}
