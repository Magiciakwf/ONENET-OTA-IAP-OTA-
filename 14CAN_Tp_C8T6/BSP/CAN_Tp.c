#include "CAN_Tp.h"
#include "MyCAN.h"
#include "Serial.h"
#include "delay.h"
#include <string.h>

#define FRAME_HEADER  0xA5
#define CMD_START     0x01
#define CMD_DATA      0x02
#define CMD_END       0x03
#define CMD_RESPONSE  0x80
#define OTA_ACK_MAGIC 0xAC

static uint8_t SN = 0;

static CanTxMsg CANTP_TxMsg = {
	.StdId = OTA_SEND_ID,
	.ExtId = 0x00000000,
	.IDE = CAN_Id_Standard,
	.RTR = CAN_RTR_Data,
	.DLC = 8,
};


//定义了个八字节大小的数组,因为塞进数组里的数据需要加工
//把数组的内容拷贝到CAN_TX结构体里，发送出去
void CANTP_Send(uint8_t *pdata, uint32_t payload_len, uint8_t delay_flag)
{
	uint8_t temp_buf[8];
	uint32_t copy_len = 0;

	(void)delay_flag;

	if(pdata == 0 || payload_len == 0 || payload_len > 4095)
	{
		return;
	}

	if(payload_len <= 7)
	{
		memset(temp_buf, 0x00, sizeof(temp_buf));
		temp_buf[0] = (uint8_t)payload_len;
		for(uint32_t i = 0; i < payload_len; i++)
		{
			temp_buf[i + 1] = *pdata++;
		}
		memcpy(CANTP_TxMsg.Data, temp_buf, sizeof(temp_buf));
		MyCAN_Transmit(&CANTP_TxMsg);
		return;
	}

	memset(temp_buf, 0x00, sizeof(temp_buf));
	temp_buf[0] = (uint8_t)(0x10 | ((payload_len >> 8) & 0x0F));
	temp_buf[1] = (uint8_t)(payload_len & 0xFF);
	for(uint32_t i = 0; i < 6; i++)
	{
		temp_buf[i + 2] = *pdata++;
	}
	copy_len = 6;
	memcpy(CANTP_TxMsg.Data, temp_buf, sizeof(temp_buf));
	MyCAN_Transmit(&CANTP_TxMsg);
	Delay_ms(2);

	SN = 1;
	while(copy_len < payload_len)
	{
		memset(temp_buf, 0x00, sizeof(temp_buf));
		temp_buf[0] = (uint8_t)(0x20 | (SN & 0x0F));
		for(uint32_t i = 0; i < 7 && copy_len < payload_len; i++)
		{
			temp_buf[i + 1] = *pdata++;
			copy_len++;
		}

		memcpy(CANTP_TxMsg.Data, temp_buf, sizeof(temp_buf));
		MyCAN_Transmit(&CANTP_TxMsg);
		SN = (SN + 1) & 0x0F;
		Delay_ms(2);
	}
}

void flow_ctr(void)
{
	uint8_t cmd;
	uint32_t payload_len;
	uint8_t *pdata;

	if(USART1_Recv_Buf[0] != FRAME_HEADER)
	{
		Serial_Printf("Frame header error\r\n");
		Rx_DoneFlag = 0;
		return;
	}

	cmd = USART1_Recv_Buf[1];
	payload_len = ((uint32_t)USART1_Recv_Buf[2] << 8) | USART1_Recv_Buf[3];
	pdata = &USART1_Recv_Buf[4];

	//取出命令类型以及有效载荷长度
	switch(cmd)
	{
		case CMD_START:
		{
			CanTxMsg First_TxMsg;
			memset(&First_TxMsg, 0, sizeof(First_TxMsg));
			First_TxMsg.StdId = OTA_START_ID;
			First_TxMsg.DLC = 5;
			First_TxMsg.RTR = CAN_RTR_Data;
			First_TxMsg.IDE = CAN_Id_Standard;
			First_TxMsg.Data[0] = 0xEE;
			//把串口接收的buffer赋给CAN结构体
			for(uint32_t i = 0; i < 4; i++)
			{
				First_TxMsg.Data[i + 1] = pdata[i];
			}
			MyCAN_Transmit(&First_TxMsg);
			break;
		}

		case CMD_DATA:
			CANTP_Send(pdata, payload_len, 0);
			break;

		case CMD_END:
		{
			CanTxMsg End_TxMsg;
			memset(&End_TxMsg, 0, sizeof(End_TxMsg));
			End_TxMsg.StdId = OTA_END_ID;
			End_TxMsg.DLC = 5;
			End_TxMsg.RTR = CAN_RTR_Data;
			End_TxMsg.IDE = CAN_Id_Standard;
			End_TxMsg.Data[0] = 0xFF;
			for(uint32_t i = 0; i < 4; i++)
			{
				End_TxMsg.Data[i + 1] = pdata[i];
			}
			MyCAN_Transmit(&End_TxMsg);
			break;
		}

		default:
			break;
	}

	Rx_DoneFlag = 0;
}

void OTA_ForwardResponse(const CanRxMsg *rx_msg)
{
	uint8_t response[8];

	if(rx_msg == 0 ||
	   rx_msg->StdId != OTA_RECV_ID ||
	   rx_msg->DLC < 5 ||
	   rx_msg->Data[0] != OTA_ACK_MAGIC)
	{
		return;
	}

	/*
	 * [A5][80][00][04][original_cmd][status][sequence_hi][sequence_lo]
	 */
	response[0] = FRAME_HEADER;
	response[1] = CMD_RESPONSE;
	response[2] = 0x00;
	response[3] = 0x04;
	response[4] = rx_msg->Data[1];
	response[5] = rx_msg->Data[2];
	response[6] = rx_msg->Data[3];
	response[7] = rx_msg->Data[4];
	Serial_SendArray(response, sizeof(response));
}
