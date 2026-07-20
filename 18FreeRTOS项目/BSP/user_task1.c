#include "user_task1.h"
#include "CAN_Tp.h"
#include "start_task.h"
#include <string.h>

static OTA_Msg_t OTA_MsgCmd;
static Job_t Job_Send;
uint32_t file_size;

void Protocol_Task(void *pvParameters)
{
	while(1)
	{
		xQueueReceive(OTA_Queue, &OTA_MsgCmd, portMAX_DELAY);
		memset(&Job_Send, 0, sizeof(Job_Send));

		switch(OTA_MsgCmd.type)
		{
			case START:
				file_size = OTA_MsgCmd.data.file_size;
				Job_Send.cmd = ERASE;
				Job_Send.needs_ack = OTA_MsgCmd.needs_ack;
				Job_Send.file_size = file_size;
				xQueueSend(Job_Queue, &Job_Send, portMAX_DELAY);
				break;

			case DATA:
				Job_Send.cmd = WRITE;
				Job_Send.needs_ack = OTA_MsgCmd.needs_ack;
				Job_Send.sequence = OTA_MsgCmd.sequence;
				Job_Send.Recv_len = OTA_MsgCmd.Recv_len;
				memcpy(Job_Send.write_buf,
					   OTA_MsgCmd.data.CANTP_RecvBuf,
					   OTA_MsgCmd.Recv_len);
				xQueueSend(Job_Queue, &Job_Send, portMAX_DELAY);
				break;

			case END:
				Job_Send.cmd = VERIFY;
				Job_Send.needs_ack = OTA_MsgCmd.needs_ack;
				Job_Send.file_size = file_size;
				Job_Send.expected_crc = ((uint32_t)OTA_MsgCmd.data.crc_buf[0] << 24) |
										((uint32_t)OTA_MsgCmd.data.crc_buf[1] << 16) |
										((uint32_t)OTA_MsgCmd.data.crc_buf[2] << 8)  |
										((uint32_t)OTA_MsgCmd.data.crc_buf[3]);
				xQueueSend(Job_Queue, &Job_Send, portMAX_DELAY);
				break;

			default:
				break;
		}
	}
}
