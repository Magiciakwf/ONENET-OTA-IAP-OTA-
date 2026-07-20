#ifndef _USER_FLASH_H_
#define _USER_FLASH_H_

#define FLASH_PAGE_SIZE 2048U
#define FLASH_PAGENUM 100U
#define USER_APP_MAX_SIZE (FLASH_PAGE_SIZE * FLASH_PAGENUM)

#include "main.h"

uint8_t user_erase_start(void);
uint8_t user_flash_write(uint32_t len,uint8_t *pBuf);
uint8_t user_flash_commit_target(void);

extern uint32_t target_addr;


#endif
