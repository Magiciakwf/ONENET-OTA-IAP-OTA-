#ifndef TEST_STM32_H
#define TEST_STM32_H
#include <stdint.h>
typedef struct
{
    uint32_t StdId;
    uint32_t ExtId;
    uint8_t IDE;
    uint8_t RTR;
    uint8_t DLC;
    uint8_t Data[8];
} CanRxMsg;
typedef CanRxMsg CanTxMsg;
#define CAN_Id_Standard 0
#define CAN_RTR_Data 0
#endif
