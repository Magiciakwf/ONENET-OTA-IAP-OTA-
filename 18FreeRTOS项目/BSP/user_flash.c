#include "user_flash.h"
#include "flash.h"
#include "ota.h"
#include "ota_cfg.h"
#include "Serial.h"

uint32_t move_addr = 0;
uint32_t target_addr = 0;

static uint8_t user_flash_select_target(void)
{
	uint8_t current_slot = get_current_slot();

	if(current_slot == 1)
	{
		target_addr = APPB_ADDR;
		Serial_Printf("Erase target: APP B\r\n");
		return 1;
	}

	if(current_slot == 2)
	{
		target_addr = APPA_ADDR;
		Serial_Printf("Erase target: APP A\r\n");
		return 1;
	}

	Serial_Printf("OTA target select error\r\n");
	target_addr = 0;
	return 0;
}

uint8_t user_flash_commit_target(void)
{
	if(target_addr == APPB_ADDR)
	{
		Reset_to_CfgB();
		Serial_Printf("Boot target: APP B\r\n");
		return 1;
	}

	if(target_addr == APPA_ADDR)
	{
		Reset_Defalt_CfgA();
		Serial_Printf("Boot target: APP A\r\n");
		return 1;
	}

	Serial_Printf("OTA target commit error\r\n");
	return 0;
}

static uint8_t user_flash_erase(void)
{
	if(user_flash_select_target() == 0)
	{
		return 0;
	}

	while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);

	flash_unlock();
	for(uint32_t i = 0; i < FLASH_PAGENUM; i++)
	{
		flash_ErasePage(target_addr + i * FLASH_PAGE_SIZE);
	}
	flash_lock();

	for(uint32_t i = 0; i < USER_APP_MAX_SIZE; i += 4)
	{
		if(*(__IO uint32_t *)(target_addr + i) != 0xFFFFFFFFU)
		{
			return 0;
		}
	}

	return 1;
}

uint8_t user_erase_start(void)
{
	move_addr = 0;
	return user_flash_erase();
}

uint8_t user_flash_write(uint32_t len, uint8_t *pBuf)
{
	uint32_t half_cnt;
	uint16_t data;

	if(target_addr == 0 || pBuf == 0 || len == 0)
	{
		return 0;
	}

	if((move_addr + len) > USER_APP_MAX_SIZE)
	{
		Serial_Printf("OTA write overflow\r\n");
		return 0;
	}

	half_cnt = len / 2;
	flash_unlock();

	for(uint32_t i = 0; i < half_cnt; i++)
	{
		data = (uint16_t)pBuf[2 * i] | ((uint16_t)pBuf[2 * i + 1] << 8);
		flash_WriteHalfWord(target_addr + move_addr + i * 2, data);
		if(*(__IO uint16_t *)(target_addr + move_addr + i * 2) != data)
		{
			flash_lock();
			return 0;
		}
	}

	if((len & 0x01) != 0)
	{
		data = (uint16_t)pBuf[len - 1] | 0xFF00;
		flash_WriteHalfWord(target_addr + move_addr + half_cnt * 2, data);
		if(*(__IO uint16_t *)(target_addr + move_addr + half_cnt * 2) != data)
		{
			flash_lock();
			return 0;
		}
	}

	flash_lock();
	move_addr += len;
	return 1;
}
