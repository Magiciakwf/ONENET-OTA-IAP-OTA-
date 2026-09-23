#ifndef USER_TEMP_H
#define USER_TEMP_H

#include "FreeRTOS.h"
#include "task.h"

/* STM32F103 数据手册典型值，可根据实际板子的标定结果修改。 */
#define HEALTH_VREFINT_MV       1200.0f
#define HEALTH_TEMP_V25_MV      1430.0f
#define HEALTH_TEMP_SLOPE_MV    4.3f

/* 演示告警阈值，不是 Flash 安全擦写的保证条件。 */
#define HEALTH_HOT_SET_C        75.0f
#define HEALTH_HOT_CLEAR_C      70.0f
#define HEALTH_LOW_SET_MV       3000.0f
#define HEALTH_LOW_CLEAR_MV     3100.0f
#define HEALTH_TIMEOUT_MS      1000U

typedef struct
{
    float temperature_c;
    float vdda_mv;
    uint8_t over_temperature;
    uint8_t low_voltage;
    uint8_t valid;
    TickType_t timestamp;
} HealthSample_t;

/* 方便在 Keil Watch 中查看；其他任务通过 Health_GetLatest 读取。 */
extern HealthSample_t g_health;
void Health_GetLatest(HealthSample_t *sample);
void user_temptask(void *pvParameters);

#endif
