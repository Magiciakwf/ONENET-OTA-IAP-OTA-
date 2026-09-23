#ifndef _CAN_TP_H_
#define _CAN_TP_H_

#include "stm32f10x.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#define CANTP_RECVSIZE 512

#define CMD_START 0x01
#define CMD_DATA  0x02
#define CMD_END   0x03

#define OTA_ACK_MAGIC       0xAC
#define OTA_STATUS_OK       0x00
#define OTA_STATUS_STATE    0x01
#define OTA_STATUS_SEQUENCE 0x02
#define OTA_STATUS_FLASH    0x03
#define OTA_STATUS_LENGTH   0x04
#define OTA_STATUS_CRC      0x05
#define OTA_STATUS_SIZE     0x06
#define OTA_STATUS_QUEUE    0x07

typedef enum{
	IDLE = 1,
	BUSY = 2,
	DONE = 3,
}CANTP_RecvStatus_t;

typedef enum{
	START = 1,
	END = 2,
	DATA = 3,

	ERASE = 4,
	WRITE = 5,
	VERIFY = 6,
}Msg_type_t;

typedef struct{
	uint8_t type;
	uint8_t needs_ack;
	uint16_t sequence;
	TaskHandle_t notify_task;
	uint32_t Recv_len;
	union{
		uint32_t file_size;
		uint8_t CANTP_RecvBuf[CANTP_RECVSIZE];
		uint8_t crc_buf[4];
	}data;
}OTA_Msg_t;

typedef struct{
	uint8_t cmd;
	uint8_t needs_ack;
	uint16_t sequence;
	TaskHandle_t notify_task;
	uint8_t write_buf[CANTP_RECVSIZE];
	uint32_t Recv_len;
	uint32_t file_size;
	uint32_t expected_crc;
}Job_t;

uint8_t CAN_TP_Recv(void);

extern uint8_t status;
extern uint32_t Recv_len;
extern volatile uint32_t CAN_QueueDropCount; /* 可在 Keil Watch 中查看 */

#endif
