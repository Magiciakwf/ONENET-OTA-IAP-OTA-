#include "user_ledtask.h"
#include "led.h"

TaskHandle_t LedTaskHandle;

/* 各模块只更新自己负责的状态，最后由 LED 任务统一决定显示什么。 */
static uint8_t network_online;
static uint8_t over_temperature;
static uint8_t low_voltage;
static uint8_t adc_error;
static LedOtaState_t ota_state = LED_OTA_IDLE;

typedef enum
{
    LED_NORMAL = 0,
    LED_OFFLINE,
    LED_DOWNLOAD,
    LED_FLASH,
    LED_VERIFY,
    LED_SUCCESS,
    LED_ERROR,
    LED_HOT,
    LED_LOW
} LedMode_t;

static void Led_Notify(uint8_t changed)
{
    if(changed && LedTaskHandle != NULL) xTaskNotifyGive(LedTaskHandle);
}

void Led_SetNetwork(uint8_t online)
{
    uint8_t changed;
    taskENTER_CRITICAL();
    changed = (network_online != online);
    network_online = online;
    taskEXIT_CRITICAL();
    Led_Notify(changed);
}

void Led_SetHealth(uint8_t hot, uint8_t low, uint8_t fault)
{
    uint8_t changed;
    taskENTER_CRITICAL();
    changed = (over_temperature != hot || low_voltage != low || adc_error != fault);
    over_temperature = hot;
    low_voltage = low;
    adc_error = fault;
    taskEXIT_CRITICAL();
    Led_Notify(changed);
}

void Led_SetOtaState(LedOtaState_t state)
{
    uint8_t changed;
    taskENTER_CRITICAL();
    changed = (ota_state != state);
    ota_state = state;
    taskEXIT_CRITICAL();
    Led_Notify(changed);
}

/* 从上往下就是显示优先级；例如低压时不会被下载灯效覆盖。 */
static LedMode_t Led_SelectMode(void)
{
    LedMode_t mode;
    taskENTER_CRITICAL();
    if(adc_error || ota_state == LED_OTA_ERROR) mode = LED_ERROR;
    else if(low_voltage) mode = LED_LOW;
    else if(over_temperature) mode = LED_HOT;
    else if(ota_state == LED_OTA_FLASH) mode = LED_FLASH;
    else if(ota_state == LED_OTA_VERIFY) mode = LED_VERIFY;
    else if(ota_state == LED_OTA_SUCCESS) mode = LED_SUCCESS;
    else if(ota_state == LED_OTA_DOWNLOAD) mode = LED_DOWNLOAD;
    else if(!network_online) mode = LED_OFFLINE;
    else mode = LED_NORMAL;
    taskEXIT_CRITICAL();
    return mode;
}

/* 返回本阶段持续多久。phase = 0、1、2...，偶数亮、奇数灭。 */
static uint32_t Led_ShowPhase(LedMode_t mode, uint8_t phase)
{
    uint32_t duration_ms;
    uint8_t on = (phase % 2 == 0);

    switch(mode)
    {
        case LED_FLASH:
            on = 1;
            duration_ms = 1000; /* 常亮，仍可被新事件立即唤醒 */
            break;
        case LED_DOWNLOAD:
            duration_ms = 200;
            break;
        case LED_VERIFY:
            duration_ms = (phase % 4 == 3) ? 700 : 100; /* 每秒双闪 */
            break;
        case LED_SUCCESS:
            duration_ms = 100;
            break;
        case LED_ERROR:
            duration_ms = (phase % 6 == 5) ? 1500 : 100; /* 三闪后停顿 */
            break;
        case LED_LOW:
            duration_ms = 250;
            break;
        case LED_HOT:
            duration_ms = 500;
            break;
        case LED_OFFLINE:
            duration_ms = (phase % 4 == 3) ? 1700 : 100; /* 每两秒双闪 */
            break;
        default:
            duration_ms = on ? 100 : 1900; /* 正常心跳 */
            break;
    }
    Led_Set(on);
    return duration_ms;
}

void user_ledtask(void *pvParameters)
{
    LedMode_t mode;
    LedMode_t previous = LED_NORMAL;
    uint8_t phase = 0;
    uint32_t duration_ms;

    (void)pvParameters;
    while(1)
    {
        mode = Led_SelectMode();
        if(mode != previous)
        {
            phase = 0;
            previous = mode;
        }
        duration_ms = Led_ShowPhase(mode, phase);
        /* 新事件到达立刻重新选择灯效；超时才推进亮灭阶段。 */
        if(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(duration_ms)) == 0)
        {
            phase = (phase + 1) % 12; /* 兼容两段、四段和六段灯效 */
        }
    }
}
