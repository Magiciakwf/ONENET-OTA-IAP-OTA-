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
volatile uint32_t CAN_QueueDropCount;

static void CANTP_SendMsgFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
	if(OTA_Queue == NULL ||
	   xQueueSendFromISR(OTA_Queue, &OTA_Msg, pxHigherPriorityTaskWoken) != pdPASS)
	{
		/* 中断不能等待队列空间；记录丢包，由发送端 ACK 超时后重发。 */
		CAN_QueueDropCount++;
	}
}

uint8_t CAN_TP_Recv(void)
{
	uint8_t pci;
	BaseType_t pxHigherPriorityTaskWoken = pdFALSE;

	if(RxMsg.IDE != CAN_Id_Standard || RxMsg.RTR != CAN_RTR_Data ||
	   RxMsg.DLC == 0 || RxMsg.DLC > 8) return 3;

	//先判断数据是否为START_ID或者END_ID
	//判断ID＋首位数据
	if(RxMsg.StdId == OTA_START_ID)
	{
		if(RxMsg.DLC < 5 || RxMsg.Data[0] != 0xEE) return 3;
		status = IDLE;
		copy_len = 0;
		memset(&OTA_Msg, 0, sizeof(OTA_Msg));
		//赋给结构体START类型
		//Needack
		//文件大小
		//调用队列ISR发送函数
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

	if(RxMsg.StdId == OTA_END_ID)
	{
		if(RxMsg.DLC < 5 || RxMsg.Data[0] != 0xFF) return 3;
		status = IDLE;
		copy_len = 0;
		//提取MSG类型
		//needack
		//赋值CRC buffer
		//通过队列ISR发送函数发出去
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


	if(RxMsg.StdId != OTA_SEND_ID) return 3;
	pci = (RxMsg.Data[0] >> 4) & 0x0F;

	if(pci == 0x00)
	{
		uint8_t single_len = RxMsg.Data[0] & 0x0F;
		/* 有效载荷至少包含 2 字节包序号和 1 字节固件。 */
		if(single_len < 3 || single_len > 7 || RxMsg.DLC < single_len + 1)
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
		status = IDLE;
		copy_len = 0;
		CANTP_SendMsgFromISR(&pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
		return 0;
	}

	//定义已拷贝长度copy_len
	//提取数据包的pci
	//提取一包的数据长度，第一个字节低四位<<8 | 第二个字节
	//提取数据后六个字节放入数组里，数组长度为512
	if(pci == 0x01)
	{
		status = IDLE;
		copy_len = 0;
		if(RxMsg.DLC != 8) return 3;
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

	//提取SN
	//提取后七位数据放在数组里
	if(pci == 0x02)
	{
		uint8_t current_sn;

		if(status != BUSY)
		{
			return 1;
		}
		/* 最后一帧允许不填充，但实际数据必须足够。 */
		if(RxMsg.DLC < 2 ||
		   (uint32_t)(RxMsg.DLC - 1) < ((Recv_len - copy_len > 7) ? 7 : Recv_len - copy_len))
		{
			status = IDLE;
			copy_len = 0;
			return 3;
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
