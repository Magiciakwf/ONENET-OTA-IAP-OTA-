#include "CAN_Tp.h"
#include "MyCAN.h"
#include "ota.h"
#include "queue.h"
#include "start_task.h"
#include <string.h>

static uint8_t CANTP_RecvBuf[CANTP_RECVSIZE];
static uint8_t Expected_SN = 0;
static uint8_t *pCANTP_RecvBuf;
uint8_t status = IDLE;
uint32_t Recv_len = 0;
uint32_t copy_len = 0;

static OTA_Msg_t OTA_Msg;

static void CANTP_SendMsgFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
	if(OTA_Queue != NULL)
	{
		xQueueSendFromISR(OTA_Queue, &OTA_Msg, pxHigherPriorityTaskWoken);
	}
}

uint8_t CAN_TP_Recv(void)
{
	uint8_t pci;
	BaseType_t pxHigherPriorityTaskWoken = pdFALSE;

	if(RxMsg.StdId == OTA_START_ID && RxMsg.Data[0] == 0xEE)
	{
		memset(&OTA_Msg, 0, sizeof(OTA_Msg));
		OTA_Msg.type = START;
		OTA_Msg.needs_ack = 1;
		OTA_Msg.data.file_size = ((uint32_t)RxMsg.Data[1] << 24) |
								 ((uint32_t)RxMsg.Data[2] << 16) |
								 ((uint32_t)RxMsg.Data[3] << 8)  |
								 ((uint32_t)RxMsg.Data[4]);
		CANTP_SendMsgFromISR(&pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
		return 0;
	}

	if(RxMsg.StdId == OTA_END_ID && RxMsg.Data[0] == 0xFF)
	{
		memset(&OTA_Msg, 0, sizeof(OTA_Msg));
		OTA_Msg.type = END;
		OTA_Msg.needs_ack = 1;
		for(int i = 0; i < 4; i++)
		{
			OTA_Msg.data.crc_buf[i] = RxMsg.Data[i + 1];
		}
		CANTP_SendMsgFromISR(&pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
		return 0;
	}

	pci = (RxMsg.Data[0] >> 4) & 0x0F;

	if(pci == 0x00)
	{
		uint8_t single_len = RxMsg.Data[0] & 0x0F;
		if(single_len > 7)
		{
			return 3;
		}

		memset(&OTA_Msg, 0, sizeof(OTA_Msg));
		OTA_Msg.type = DATA;
		OTA_Msg.needs_ack = 1;
		if(single_len >= 2)
		{
			OTA_Msg.sequence = ((uint16_t)RxMsg.Data[1] << 8) |
							   RxMsg.Data[2];
			OTA_Msg.Recv_len = single_len - 2;
			memcpy(OTA_Msg.data.CANTP_RecvBuf,
				   &RxMsg.Data[3],
				   OTA_Msg.Recv_len);
		}
		Recv_len = single_len;
		CANTP_SendMsgFromISR(&pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
		return 0;
	}

	if(pci == 0x01)
	{
		copy_len = 0;
		OTA_Msg.Recv_len = (((uint32_t)RxMsg.Data[0] & 0x0F) << 8) | RxMsg.Data[1];
		Recv_len = OTA_Msg.Recv_len;

		if(Recv_len > CANTP_RECVSIZE || Recv_len <= 6)
		{
			status = IDLE;
			return 4;
		}

		pCANTP_RecvBuf = CANTP_RecvBuf;
		for(int i = 0; i < 6; i++)
		{
			*pCANTP_RecvBuf++ = RxMsg.Data[i + 2];
			copy_len++;
		}
		status = BUSY;
		Expected_SN = 1;
		return 0;
	}

	if(pci == 0x02)
	{
		uint8_t current_sn;

		if(status != BUSY)
		{
			return 1;
		}

		current_sn = RxMsg.Data[0] & 0x0F;
		if(current_sn != Expected_SN)
		{
			status = IDLE;
			copy_len = 0;
			return 2;
		}

		Expected_SN++;
		Expected_SN %= 16;

		for(int i = 0; i < 7; i++)
		{
			if(copy_len < Recv_len)
			{
				*pCANTP_RecvBuf++ = RxMsg.Data[i + 1];
				copy_len++;
			}
		}

		if(copy_len >= Recv_len)
		{
			memset(&OTA_Msg, 0, sizeof(OTA_Msg));
			OTA_Msg.type = DATA;
			OTA_Msg.needs_ack = 1;
			if(Recv_len >= 2)
			{
				OTA_Msg.sequence = ((uint16_t)CANTP_RecvBuf[0] << 8) |
								   CANTP_RecvBuf[1];
				OTA_Msg.Recv_len = Recv_len - 2;
				memcpy(OTA_Msg.data.CANTP_RecvBuf,
					   &CANTP_RecvBuf[2],
					   OTA_Msg.Recv_len);
			}
			CANTP_SendMsgFromISR(&pxHigherPriorityTaskWoken);
			status = IDLE;
			copy_len = 0;
			portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
			return 0;
		}
	}

	return 5;
}
