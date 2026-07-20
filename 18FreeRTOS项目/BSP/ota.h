#ifndef _OTA_H_
#define _OTA_H_

#include "stm32f10x.h"

void ota_update(void);
uint8_t get_current_slot(void);
uint32_t OTA_CRC32_Init(void);
uint32_t OTA_CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t OTA_CRC32_Finish(uint32_t crc);
uint32_t OTA_Calculate_CRC32(uint32_t start_addr, uint32_t len);
void CRC_Compare(void);

extern uint8_t CRC_RxBuf[4];

#endif
