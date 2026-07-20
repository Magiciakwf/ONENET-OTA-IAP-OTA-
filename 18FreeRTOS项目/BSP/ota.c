#include "ota.h"
#include "CAN_Tp.h"
#include "Serial.h"
#include "ota_cfg.h"
#include "user_Ymodem.h"

uint8_t CRC_RxBuf[4];

uint8_t get_current_slot(void)
{
	if(SCB->VTOR == APPA_ADDR)
	{
		return 1;
	}

	if(SCB->VTOR == APPB_ADDR)
	{
		return 2;
	}

	return 0;
}

void ota_update(void)
{
	uint8_t current_slot = get_current_slot();
	uint32_t target_addr = 0;

	if(current_slot == 1)
	{
		target_addr = APPB_ADDR;
		Serial_Printf("Now is A area, prepare to update B area\r\n");
	}
	else if(current_slot == 2)
	{
		target_addr = APPA_ADDR;
		Serial_Printf("Now is B area, prepare to update A area\r\n");
	}
	else
	{
		Serial_Printf("VTOR addr error\r\n");
	}

	if(target_addr != 0)
	{
		ymodem_start();
		while(1)
		{
			int status = ymodem_recv_process();
			if(status < 0)
			{
				Serial_Printf("Config Error\r\n");
				while(1);
			}

			if(Transfer_Done_Flag == 1)
			{
				Serial_Printf("Switch to update area\r\n");
				Transfer_Done_Flag = 0;
				NVIC_SystemReset();
			}
		}
	}
}

uint32_t OTA_CRC32_Init(void)
{
	return 0xFFFFFFFFU;
}

uint32_t OTA_CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
	for(uint32_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for(uint32_t j = 0; j < 8; j++)
		{
			if((crc & 1U) != 0U)
			{
				crc = (crc >> 1) ^ 0xEDB88320U;
			}
			else
			{
				crc >>= 1;
			}
		}
	}
	return crc;
}

uint32_t OTA_CRC32_Finish(uint32_t crc)
{
	return ~crc;
}

uint32_t OTA_Calculate_CRC32(uint32_t start_addr, uint32_t len)
{
	uint32_t crc = OTA_CRC32_Init();

	for(uint32_t i = 0; i < len; i++)
	{
		uint8_t byte = *(__IO uint8_t *)(start_addr + i);
		crc = OTA_CRC32_Update(crc, &byte, 1);
	}

	return OTA_CRC32_Finish(crc);
}
