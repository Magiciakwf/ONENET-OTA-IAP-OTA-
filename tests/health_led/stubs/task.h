#ifndef TEST_TASK_H
#define TEST_TASK_H
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout);
void vTaskDelay(TickType_t ticks);
#endif
