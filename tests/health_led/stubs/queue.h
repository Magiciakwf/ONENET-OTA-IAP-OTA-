#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *wake);
#endif
