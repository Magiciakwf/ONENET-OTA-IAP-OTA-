#ifndef USER_ADC_H
#define USER_ADC_H

#include "stm32f10x.h"

#define ADC_SAMPLE_HZ       100U
#define ADC_CHANNEL_COUNT  2U
#define ADC_AVERAGE_COUNT  16U
#define ADC_BLOCK_SIZE     (ADC_CHANNEL_COUNT * ADC_AVERAGE_COUNT)

/* 由健康监测任务调用；初始化返回 0 表示校准超时。 */
uint8_t UserADC_Init(void);
uint8_t UserADC_ReadBlock(uint16_t *buffer);

#endif
