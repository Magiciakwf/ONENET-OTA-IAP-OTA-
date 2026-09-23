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
#include "esp8266.h"

QueueHandle_t OTA_Queue;
QueueHandle_t Job_Queue;

SemaphoreHandle_t PrintfMutex;

/* 队列满/等待 ACK 时任务会阻塞，低优先级的 Flash 任务才能继续运行。 */
static void CheckCreated(BaseType_t result)
{
	if(result != pdPASS)
	{
		Serial_Printf("RTOS object creation failed\r\n");
		taskDISABLE_INTERRUPTS();
		Led_Set(1);
		while(1) {}
	}
}


void start_task(void *pvParameters)
{
	Serial_Init();//初始化串口1
	MyCan_Init();
	KEY_Init();
	Led_Init();
	ESP8266_Init(ESP8266_DEFAULT_BAUDRATE);//初始化串口二
	
	OTA_Queue = xQueueCreate(10,sizeof(OTA_Msg_t)); // 第一层：接收到的 OTA 消息
	Job_Queue = xQueueCreate(10,sizeof(Job_t));
	
	PrintfMutex = xSemaphoreCreateMutex();
	CheckCreated(OTA_Queue != NULL && Job_Queue != NULL && PrintfMutex != NULL);
	if(((SystemConfig_t *)PARM_ADDR)->magic_num == MAGIC_NUM &&
	   ((SystemConfig_t *)PARM_ADDR)->ota_state == OTA_STATUS_TEXTING)
	{
		Ota_State_Set(OTA_STATUS_NORMAL);
	}
	
	taskENTER_CRITICAL();
	CheckCreated(xTaskCreate(Protocol_Task,"Protocol_Task",128*2,NULL,5,NULL));
	CheckCreated(xTaskCreate(Flash_Write_Task,"Flash_Write_Task",128*2,NULL,1,NULL));
	CheckCreated(xTaskCreate(OneNET_OTA_Task,"OneNET_OTA_Task",128*8,NULL,4,NULL));
	CheckCreated(xTaskCreate(user_temptask,"Health_Task",128*4,NULL,2,NULL));
	/* LED 任务只做 GPIO 和短判断，及时显示 Flash 操作开始的状态。 */
	CheckCreated(xTaskCreate(user_ledtask,"LED_Task",128*2,NULL,3,&LedTaskHandle));
	taskEXIT_CRITICAL();
	vTaskDelete(NULL);
	
}
