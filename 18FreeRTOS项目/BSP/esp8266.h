#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f10x.h"

#ifndef ESP8266_USART
#define ESP8266_USART USART2
#define ESP8266_USART_RCC RCC_APB1Periph_USART2
#define ESP8266_USART_RCC_CMD RCC_APB1PeriphClockCmd
#define ESP8266_GPIO GPIOA
#define ESP8266_GPIO_RCC RCC_APB2Periph_GPIOA
#define ESP8266_TX_PIN GPIO_Pin_2
#define ESP8266_RX_PIN GPIO_Pin_3
#endif

#define ESP8266_DEFAULT_BAUDRATE 115200U

void ESP8266_Init(uint32_t baudrate);
void ESP8266_SendRaw(const uint8_t *data, uint32_t len);
int ESP8266_ReadByte(uint8_t *data, uint32_t timeout_ms);
void ESP8266_Flush(uint32_t quiet_ms);
int ESP8266_WaitForString(const char *expect, uint32_t timeout_ms);
int ESP8266_SendCommand(const char *cmd, const char *expect, uint32_t timeout_ms);
int ESP8266_JoinAP(const char *ssid, const char *password, uint32_t timeout_ms);
int ESP8266_OpenTcp(const char *host, uint16_t port, uint32_t timeout_ms);
void ESP8266_CloseTcp(void);
int ESP8266_SendTcpData(const char *data, uint32_t len, uint32_t timeout_ms);

#endif
