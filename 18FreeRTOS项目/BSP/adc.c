#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

/* 每组两个通道；DMA 填一半时，中断复制已完成的另一半。 */
static volatile uint16_t dma_buf[ADC_BLOCK_SIZE * 2];//64的大小，16个半字触发半传输中断
static volatile uint16_t completed_buf[ADC_BLOCK_SIZE];
static volatile uint8_t block_valid;
static TaskHandle_t sample_task;

uint8_t UserADC_Init(void)
{
    ADC_InitTypeDef adc;
    DMA_InitTypeDef dma;
    TIM_TimeBaseInitTypeDef timer;
    NVIC_InitTypeDef nvic;
    RCC_ClocksTypeDef clocks;
    uint32_t timer_clock;
    uint32_t timeout;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6); /* 72 MHz / 6 = 12 MHz */

    /* TIM3 每 10 ms 发出一次 TRGO，不需要定时器中断或外部引脚。 */
    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    if(clocks.PCLK1_Frequency != clocks.HCLK_Frequency) timer_clock *= 2;
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = (uint16_t)(timer_clock / 10000U - 1U);
    timer.TIM_Period = 10000U / ADC_SAMPLE_HZ - 1U;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);

    ADC_StructInit(&adc);
    adc.ADC_ScanConvMode = ENABLE;
    adc.ADC_ContinuousConvMode = DISABLE; /* 每次触发只扫描一组 */
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
    adc.ADC_NbrOfChannel = ADC_CHANNEL_COUNT;
    ADC_Init(ADC1, &adc);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_16, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_17, 2, ADC_SampleTime_239Cycles5);
    ADC_TempSensorVrefintCmd(ENABLE);

    DMA_DeInit(DMA1_Channel1);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)dma_buf;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = ADC_BLOCK_SIZE * 2;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_Medium;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE, ENABLE);

    nvic.NVIC_IRQChannel = DMA1_Channel1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 6; /* 可调用 FreeRTOS FromISR API */
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    ADC_Cmd(ADC1, ENABLE);
    vTaskDelay(pdMS_TO_TICKS(1)); /* 等待 ADC 和内部传感器稳定 */
    ADC_ResetCalibration(ADC1);
    timeout = 100000U;
    while(ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
        if(--timeout == 0) return 0;
    }
    ADC_StartCalibration(ADC1);
    timeout = 100000U;
    while(ADC_GetCalibrationStatus(ADC1) != RESET)
    {
        if(--timeout == 0) return 0;
    }

    /* 在健康任务自身启动采样，保证中断通知的目标已经存在。 */
    sample_task = xTaskGetCurrentTaskHandle();
    DMA_ClearFlag(DMA1_FLAG_GL1);
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    TIM_SetCounter(TIM3, 0);
    TIM_Cmd(TIM3, ENABLE);
    return 1;
}

uint8_t UserADC_ReadBlock(uint16_t *buffer)
{
    uint16_t i;
    uint8_t valid;

    /* 只保护几十次内存拷贝，计算和打印放在临界区外。 */
    taskENTER_CRITICAL();
    valid = block_valid;
    for(i = 0; i < ADC_BLOCK_SIZE; i++) buffer[i] = completed_buf[i];
    taskEXIT_CRITICAL();
    return valid;
}

void DMA1_Channel1_IRQHandler(void)
{
    BaseType_t wake = pdFALSE;
    uint16_t start;
    uint16_t i;

    if(DMA_GetITStatus(DMA1_IT_TE1) != RESET)
    {
        DMA_ClearFlag(DMA1_FLAG_GL1);
        TIM_Cmd(TIM3, DISABLE);
        DMA_Cmd(DMA1_Channel1, DISABLE);
        block_valid = 0;
        if(sample_task != NULL) vTaskNotifyGiveFromISR(sample_task, &wake);
    }
    else if(DMA_GetITStatus(DMA1_IT_HT1) != RESET ||
            DMA_GetITStatus(DMA1_IT_TC1) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_HT1 | DMA1_IT_TC1);
        /* 延迟时丢弃旧半块，只取当前没有被 DMA 写入的最新完整半块。 */
        start = DMA_GetCurrDataCounter(DMA1_Channel1) > ADC_BLOCK_SIZE ? ADC_BLOCK_SIZE : 0;
        for(i = 0; i < ADC_BLOCK_SIZE; i++) completed_buf[i] = dma_buf[start + i];
        /* 如果恰好在复制时切换了半块，丢弃这次数据，等待下一块。 */
        block_valid = (start == (DMA_GetCurrDataCounter(DMA1_Channel1) > ADC_BLOCK_SIZE ? ADC_BLOCK_SIZE : 0));
        if(sample_task != NULL) vTaskNotifyGiveFromISR(sample_task, &wake);
    }
    portYIELD_FROM_ISR(wake);
}
