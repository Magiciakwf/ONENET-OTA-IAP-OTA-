#include "user_task2.h"
#include "CAN_Tp.h"
#include "MyCan.h"
#include "Serial.h"
#include "ota.h"
#include "queue.h"
#include "start_task.h"
#include "user_flash.h"
#include "user_ledtask.h"
#include <string.h>

static Job_t Job_Recv;
static uint8_t session_active = 0;
static uint16_t expected_sequence = 0;
static uint32_t expected_file_size = 0;
static uint32_t received_size = 0;

static void OTA_SendResponse(uint8_t cmd, uint8_t result, uint16_t sequence)
{
	CanTxMsg tx_msg;

	if(result != OTA_STATUS_OK) Led_SetOtaState(LED_OTA_ERROR);

	memset(&tx_msg, 0, sizeof(tx_msg));
	tx_msg.StdId = OTA_RECV_ID;
	tx_msg.IDE = CAN_Id_Standard;
	tx_msg.RTR = CAN_RTR_Data;
	tx_msg.DLC = 5;
	tx_msg.Data[0] = OTA_ACK_MAGIC;
	tx_msg.Data[1] = cmd;
	tx_msg.Data[2] = result;
	tx_msg.Data[3] = (uint8_t)(sequence >> 8);
	tx_msg.Data[4] = (uint8_t)sequence;
	MyCAN_Transmit(&tx_msg);
}

static void OTA_ReplyIfNeeded(const Job_t *job,
							  uint8_t cmd,
							  uint8_t result,
							  uint16_t sequence)
{
	//判断升级方式,job->needs_ack表示本地OTA升级方式
	if(result != OTA_STATUS_OK) Led_SetOtaState(LED_OTA_ERROR);
	else if(cmd == CMD_END) Led_SetOtaState(LED_OTA_SUCCESS);
	else Led_SetOtaState(LED_OTA_DOWNLOAD);

	if(job->needs_ack)
	{
		OTA_SendResponse(cmd, result, sequence);
	}

	if(job->notify_task != NULL)
	{
		xTaskNotify(job->notify_task, result, eSetValueWithOverwrite);
	}
}


//不erase完的话不会进入下一个接收队列的那里
//但是write不是一次就write完毕的
void Flash_Write_Task(void *pvParameters)
{
	while(1)
	{
		xQueueReceive(Job_Queue, &Job_Recv, portMAX_DELAY);

		switch(Job_Recv.cmd)
		{
			case ERASE:
			//判断文件大小是否为0或者文件大小是否超过分区大小
				if(Job_Recv.file_size == 0 ||
				   Job_Recv.file_size > USER_APP_MAX_SIZE)
				{
					//是的话就报告文件长度非法ACK
					session_active = 0;
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_START,
									  OTA_STATUS_LENGTH,
									  0);
					break;
				}
				//判断是否擦除成功，不成功则报错擦除失败
				Led_SetOtaState(LED_OTA_FLASH);
				if(user_erase_start() == 0)
				{
					session_active = 0;
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_START,
									  OTA_STATUS_FLASH,
									  0);
					break;
				}
				//最后存储发过来的总固件大小，并ACK正常
				expected_file_size = Job_Recv.file_size;
				received_size = 0;
				expected_sequence = 0;
				session_active = 1;
				OTA_ReplyIfNeeded(&Job_Recv,
								  CMD_START,
								  OTA_STATUS_OK,
								  0);
				break;

			case WRITE:
				if(!session_active)
				{
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_DATA,
									  OTA_STATUS_STATE,
									  Job_Recv.sequence);
					break;
				}

				/*
				 * A duplicate is caused by a lost ACK. Re-ACK it without
				 * writing the same bytes to Flash a second time.
				 */
				if(Job_Recv.needs_ack &&
				   Job_Recv.Recv_len == 0)
				{
					OTA_SendResponse(CMD_DATA,
									 OTA_STATUS_LENGTH,
									 Job_Recv.sequence);
					break;
				}

				if(Job_Recv.needs_ack &&
				   expected_sequence != 0 &&
				   Job_Recv.sequence == (uint16_t)(expected_sequence - 1))
				{
					OTA_SendResponse(CMD_DATA,
									 OTA_STATUS_OK,
									 Job_Recv.sequence);
					break;
				}

				if(Job_Recv.needs_ack &&
				   Job_Recv.sequence != expected_sequence)
				{
					OTA_SendResponse(CMD_DATA,
									 OTA_STATUS_SEQUENCE,
									 Job_Recv.sequence);
					break;
				}

				if(Job_Recv.Recv_len == 0 ||
				   received_size + Job_Recv.Recv_len > expected_file_size)
				{
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_DATA,
									  OTA_STATUS_LENGTH,
									  Job_Recv.sequence);
					break;
				}

				Led_SetOtaState(LED_OTA_FLASH);
				if(user_flash_write(Job_Recv.Recv_len,
									Job_Recv.write_buf) == 0)
				{
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_DATA,
									  OTA_STATUS_FLASH,
									  Job_Recv.sequence);
					break;
				}

				received_size += Job_Recv.Recv_len;
				OTA_ReplyIfNeeded(&Job_Recv,
								  CMD_DATA,
								  OTA_STATUS_OK,
								  Job_Recv.sequence);
				if(Job_Recv.needs_ack)
				{
					expected_sequence++;
				}
				break;

			case VERIFY:
			{
				uint32_t calculate_crc;

				if(!session_active)
				{
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_END,
									  OTA_STATUS_STATE,
									  expected_sequence);
					break;
				}

				if(received_size != expected_file_size ||
				   Job_Recv.file_size != expected_file_size)
				{
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_END,
									  OTA_STATUS_SIZE,
									  expected_sequence);
					break;
				}

				Led_SetOtaState(LED_OTA_VERIFY);
				calculate_crc = OTA_Calculate_CRC32(target_addr,
												   expected_file_size);
				if(Job_Recv.expected_crc != calculate_crc)
				{
					Serial_Printf("OTA download error, expect:%08X calc:%08X\r\n",
								  Job_Recv.expected_crc,
								  calculate_crc);
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_END,
									  OTA_STATUS_CRC,
									  expected_sequence);
					break;
				}

				if(user_flash_commit_target() == 0)
				{
					OTA_ReplyIfNeeded(&Job_Recv,
									  CMD_END,
									  OTA_STATUS_FLASH,
									  expected_sequence);
					break;
				}

				Serial_Printf("OTA download success\r\n");
				session_active = 0;
				OTA_ReplyIfNeeded(&Job_Recv,
								  CMD_END,
								  OTA_STATUS_OK,
								  expected_sequence);

				/* ACK 已发送；给成功灯效留 600 ms（快闪三次）再重启。 */
				vTaskDelay(pdMS_TO_TICKS(600));
				NVIC_SystemReset();
				break;
			}

			default:
				break;
		}
	}
}
