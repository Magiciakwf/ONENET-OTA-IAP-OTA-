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


//解锁flash
//erase，erase大小为200k
//上锁flash
//检查是否擦除完成(转成volatile uint32位指针类型,再进行解引用，判断值是否为全F)
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
		if(*(volatile uint32_t *)(target_addr + i) != 0xFFFFFFFFU)
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

//解锁flash
//每次写半个字（2个字节）
//拼接写入数据，buf+1左移8位放在高位，buf放在低八位
//写入位置等于目标分区起始地址+偏移地址（每次就处理一个队列成员的数据)+i
//最后如果数据长度为奇数，单独写入一个字节
//上锁flash
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
