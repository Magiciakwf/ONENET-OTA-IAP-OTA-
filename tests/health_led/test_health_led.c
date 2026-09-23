/* PC 上验证真实业务代码；这里只替换 MCU/RTOS 接口，不复制算法。 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../18FreeRTOS项目/BSP/user_temp.c"
#include "../../18FreeRTOS项目/BSP/user_ledtask.c"

static TickType_t now;
static unsigned notifications;
static uint8_t led_on;
TickType_t xTaskGetTickCount(void) { return now; }
BaseType_t xTaskNotifyGive(TaskHandle_t task) { (void)task; notifications++; return pdTRUE; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout)
{ (void)clear; now += timeout; return 0; }
void vTaskDelay(TickType_t ticks) { now += ticks; }
uint8_t UserADC_Init(void) { return 1; }
uint8_t UserADC_ReadBlock(uint16_t *buffer) { (void)buffer; return 0; }
void Serial_Printf(char *format, ...) { (void)format; }
void Led_Set(uint8_t on) { led_on = on; }

static void MakeSamples(uint16_t *buffer, float temp, float supply)
{
    unsigned i;
    float sense = HEALTH_TEMP_V25_MV - (temp - 25.0f) * HEALTH_TEMP_SLOPE_MV;
    for(i = 0; i < ADC_AVERAGE_COUNT; i++)
    {
        buffer[2 * i] = (uint16_t)(sense * 4095.0f / supply + 0.5f);
        buffer[2 * i + 1] = (uint16_t)(HEALTH_VREFINT_MV * 4095.0f / supply + 0.5f);
    }
}

int main(void)
{
    uint16_t buffer[ADC_BLOCK_SIZE];
    HealthSample_t sample = {0};
    HealthSample_t snapshot;
    unsigned before;
    unsigned i;

    /* 不同供电电压下，同一温度经补偿后应接近一致。 */
    MakeSamples(buffer, 25.0f, 3300.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(sample.valid && fabsf(sample.temperature_c - 25.0f) < 0.3f);
    assert(fabsf(sample.vdda_mv - 3300.0f) < 3.0f);
    MakeSamples(buffer, 25.0f, 2900.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(fabsf(sample.temperature_c - 25.0f) < 0.3f && sample.low_voltage);
    MakeSamples(buffer, 25.0f, 3050.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(sample.low_voltage); /* 滞回区内保持告警 */
    MakeSamples(buffer, 25.0f, 3150.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(!sample.low_voltage);

    MakeSamples(buffer, 80.0f, 3300.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(sample.over_temperature);
    MakeSamples(buffer, 73.0f, 3300.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(sample.over_temperature);
    MakeSamples(buffer, 68.0f, 3300.0f);
    Health_ProcessBlock(buffer, &sample);
    assert(!sample.over_temperature);

    /* 对称噪声在 16 组均值中抵消。 */
    MakeSamples(buffer, 25.0f, 3300.0f);
    for(i = 0; i < ADC_AVERAGE_COUNT; i++) buffer[2 * i] += (i % 2 ? 20 : -20);
    Health_ProcessBlock(buffer, &sample);
    assert(fabsf(sample.temperature_c - 25.0f) < 0.3f);
    buffer[1] = 0;
    Health_ProcessBlock(buffer, &sample);
    assert(!sample.valid);
    MakeSamples(buffer, 25.0f, 3300.0f);
    buffer[0] = 4095;
    Health_ProcessBlock(buffer, &sample);
    assert(!sample.valid);

    /* 过期数据以及 tick 溢出时，读取者必须得到 invalid。 */
    MakeSamples(buffer, 25.0f, 3300.0f);
    now = UINT32_MAX - 500;
    Health_ProcessBlock(buffer, &sample);
    g_health = sample;
    now += 900;
    Health_GetLatest(&snapshot);
    assert(snapshot.valid);
    now += 200;
    Health_GetLatest(&snapshot);
    assert(!snapshot.valid);

    LedTaskHandle = (TaskHandle_t)1;
    Led_SetNetwork(1);
    Led_SetHealth(0, 0, 0);
    Led_SetOtaState(LED_OTA_IDLE);
    assert(Led_SelectMode() == LED_NORMAL);
    before = notifications;
    Led_SetNetwork(1);
    assert(notifications == before); /* 状态未变，不反复唤醒 */
    Led_SetNetwork(0);
    assert(Led_SelectMode() == LED_OFFLINE);
    Led_SetOtaState(LED_OTA_DOWNLOAD);
    assert(Led_SelectMode() == LED_DOWNLOAD);
    Led_SetOtaState(LED_OTA_FLASH);
    assert(Led_SelectMode() == LED_FLASH);
    Led_SetOtaState(LED_OTA_VERIFY);
    assert(Led_SelectMode() == LED_VERIFY);
    Led_SetHealth(1, 1, 0);
    assert(Led_SelectMode() == LED_LOW);
    Led_SetHealth(1, 0, 0);
    assert(Led_SelectMode() == LED_HOT);
    Led_SetHealth(0, 0, 1);
    assert(Led_SelectMode() == LED_ERROR);
    Led_SetHealth(0, 0, 0);
    assert(Led_SelectMode() == LED_VERIFY); /* 告警清除后恢复升级阶段 */
    Led_SetOtaState(LED_OTA_ERROR);
    assert(Led_SelectMode() == LED_ERROR);
    Led_SetOtaState(LED_OTA_DOWNLOAD);
    assert(Led_SelectMode() == LED_DOWNLOAD);
    Led_SetOtaState(LED_OTA_SUCCESS);
    assert(Led_SelectMode() == LED_SUCCESS);

    assert(Led_ShowPhase(LED_NORMAL, 0) == 100 && led_on);
    assert(Led_ShowPhase(LED_NORMAL, 1) == 1900 && !led_on);
    assert(Led_ShowPhase(LED_OFFLINE, 3) == 1700 && !led_on);
    assert(Led_ShowPhase(LED_ERROR, 5) == 1500 && !led_on);
    assert(Led_ShowPhase(LED_FLASH, 1) == 1000 && led_on);
    puts("PASS: voltage compensation, average filter, hysteresis, invalid/stale data, LED priority and patterns");
    return 0;
}
