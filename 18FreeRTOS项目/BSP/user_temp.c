#include "user_temp.h"
#include "adc.h"
#include "user_ledtask.h"
#include "Serial.h"

HealthSample_t g_health;

void Health_GetLatest(HealthSample_t *sample)
{
    taskENTER_CRITICAL();
    *sample = g_health;
    taskEXIT_CRITICAL();
    if((xTaskGetTickCount() - sample->timestamp) > pdMS_TO_TICKS(HEALTH_TIMEOUT_MS))
    {
        sample->valid = 0;
    }
}

/* 16 组算术平均就是数字滤波，不再叠加其他算法。 */
static void Health_ProcessBlock(const uint16_t *buffer, HealthSample_t *sample)
{
    uint32_t temp_sum = 0;
    uint32_t vref_sum = 0;
    uint16_t i;
    float temp_raw;
    float vref_raw;
    float sense_mv;

    sample->valid = 0;
    for(i = 0; i < ADC_AVERAGE_COUNT; i++)
    {
        /* 零值/满量程视为无效，避免除零或使用异常测量值。 */
        if(buffer[2 * i] == 0 || buffer[2 * i] >= 4095 ||
           buffer[2 * i + 1] == 0 || buffer[2 * i + 1] >= 4095) return;
        temp_sum += buffer[2 * i];
        vref_sum += buffer[2 * i + 1];
    }
    temp_raw = (float)temp_sum / ADC_AVERAGE_COUNT;
    vref_raw = (float)vref_sum / ADC_AVERAGE_COUNT;

    /* 默认板上 VREF+ 接 VDDA；若参考引脚独立供电，测得的是 VREF+。 */
    sample->vdda_mv = HEALTH_VREFINT_MV * 4095.0f / vref_raw;
    sense_mv = temp_raw * sample->vdda_mv / 4095.0f;
    sample->temperature_c = (HEALTH_TEMP_V25_MV - sense_mv) / HEALTH_TEMP_SLOPE_MV + 25.0f;

    /* 进入/退出阈值不同，防止在临界值附近来回报警。 */
    if(sample->temperature_c >= HEALTH_HOT_SET_C) sample->over_temperature = 1;
    else if(sample->temperature_c <= HEALTH_HOT_CLEAR_C) sample->over_temperature = 0;
    if(sample->vdda_mv <= HEALTH_LOW_SET_MV) sample->low_voltage = 1;
    else if(sample->vdda_mv >= HEALTH_LOW_CLEAR_MV) sample->low_voltage = 0;
    sample->timestamp = xTaskGetTickCount();
    sample->valid = 1;
}

void user_temptask(void *pvParameters)
{
    uint16_t buffer[ADC_BLOCK_SIZE];
    HealthSample_t sample = {0};
    TickType_t last_print = 0;
    uint8_t adc_ready;

    (void)pvParameters;
    adc_ready = UserADC_Init();
    while(1)
    {
        if(adc_ready && ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEALTH_TIMEOUT_MS)) != 0 &&
           UserADC_ReadBlock(buffer))
        {
            Health_ProcessBlock(buffer, &sample);
        }
        else
        {
            sample.valid = 0;
            if(!adc_ready) vTaskDelay(pdMS_TO_TICKS(HEALTH_TIMEOUT_MS));
        }

        taskENTER_CRITICAL();
        g_health = sample;
        taskEXIT_CRITICAL();
        Led_SetHealth(sample.over_temperature, sample.low_voltage, !sample.valid);

        /* 每 160 ms 处理一块，串口只每秒输出一次。 */
        if((xTaskGetTickCount() - last_print) >= pdMS_TO_TICKS(1000))
        {
            last_print = xTaskGetTickCount();
            if(sample.valid)
            {
                Serial_Printf("Health: %.1f C, %.0f mV, hot=%u low=%u\r\n",
                              sample.temperature_c, sample.vdda_mv,
                              (unsigned int)sample.over_temperature, (unsigned int)sample.low_voltage);
            }
            else Serial_Printf("Health: ADC data invalid or timeout\r\n");
        }
    }
}
