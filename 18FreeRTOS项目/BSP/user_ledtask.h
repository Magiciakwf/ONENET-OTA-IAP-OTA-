#ifndef USER_LEDTASK_H
#define USER_LEDTASK_H

#include "FreeRTOS.h"
#include "task.h"

typedef enum
{
    LED_OTA_IDLE = 0,
    LED_OTA_DOWNLOAD,
    LED_OTA_FLASH,
    LED_OTA_VERIFY,
    LED_OTA_SUCCESS,
    LED_OTA_ERROR
} LedOtaState_t;

extern TaskHandle_t LedTaskHandle;

/* 这三个接口只供任务调用，内部会通知 LED 任务重新选择灯效。 */
void Led_SetNetwork(uint8_t online);
void Led_SetHealth(uint8_t hot, uint8_t low, uint8_t adc_fault);
void Led_SetOtaState(LedOtaState_t state);
void user_ledtask(void *pvParameters);

#endif
