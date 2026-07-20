#include "start_task.h"
#include "Serial.h"
#include "user_task1.h"
#include "user_task2.h"
#include "CAN_Tp.h"
#include "MyCan.h"
#include "key.h"
#include "adc.h"
#include "user_temp.h"
#include "led.h"
#include "user_ledtask.h"
#include "ota_cfg.h"
#include "onenet_ota.h"


QueueHandle_t OTA_Queue;
QueueHandle_t Job_Queue;

SemaphoreHandle_t PrintfMutex;


void start_task(void *pvParameters)
{
	Serial_Init();
	MyCan_Init();
	KEY_Init();
	UserADC_Init();
	Led_Init();
	
	OTA_Queue = xQueueCreate(10,sizeof(OTA_Msg_t));
	Job_Queue = xQueueCreate(10,sizeof(Job_t));
	
	PrintfMutex = xSemaphoreCreateMutex();
	if(((SystemConfig_t *)PARM_ADDR)->magic_num == MAGIC_NUM &&
	   ((SystemConfig_t *)PARM_ADDR)->ota_state == OTA_STATUS_TEXTING)
	{
		Ota_State_Set(OTA_STATUS_NORMAL);
	}
	
	taskENTER_CRITICAL();
	xTaskCreate(Protocol_Task,"Protocol_Task",128*2,NULL,1,NULL);
	xTaskCreate(Flash_Write_Task,"Flash_Write_Task",128*2,NULL,1,NULL);
	xTaskCreate(OneNET_OTA_Task,"OneNET_OTA_Task",128*8,NULL,1,NULL);
	xTaskCreate(user_temptask,"user_temptask",128*2,NULL,1,NULL);
	xTaskCreate(user_ledtask,"user_ledtask",128*2,NULL,1,NULL);
	taskEXIT_CRITICAL();
	vTaskDelete(NULL);
	
}
