#include "stm32f10x.h"
#include "main.h"
#include "MyCAN.h"
#include "Serial.h"
#include "key.h"
#include "CAN_Tp.h"
#include <string.h>

volatile uint32_t g_SystemTick_ms = 0;

int main(void)
{
	CanRxMsg RxMsg;

	Serial_Init();
	MyCan_Init();
	KEY_Init();
	memset(&RxMsg, 0, sizeof(RxMsg));

	while(1)
	{
		if(Rx_DoneFlag == 1)
		{
			flow_ctr();
		}

		Call_BusOff_Recovery_Process();

		if(MyCAN_RecvFlag() == 1)
		{
			MyCAN_Receive(&RxMsg);
			if(RxMsg.StdId == OTA_RECV_ID)
			{
				OTA_ForwardResponse(&RxMsg);
			}
		}
	}
}
