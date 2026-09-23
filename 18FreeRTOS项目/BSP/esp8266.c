#include "esp8266.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static void ESP8266_SendByte(uint8_t data)
{
	USART_SendData(ESP8266_USART, data);
	while(USART_GetFlagStatus(ESP8266_USART, USART_FLAG_TXE) == RESET);
}

//1. 开启 USART2 时钟
//开启GPIO A时钟
//3. PA2 配置为串口发送
//P3配置为串口接收
//配置串口参数
void ESP8266_Init(uint32_t baudrate)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;

	ESP8266_USART_RCC_CMD(ESP8266_USART_RCC, ENABLE);
	RCC_APB2PeriphClockCmd(ESP8266_GPIO_RCC, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = ESP8266_TX_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(ESP8266_GPIO, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = ESP8266_RX_PIN;
	GPIO_Init(ESP8266_GPIO, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate = baudrate;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(ESP8266_USART, &USART_InitStructure);
	USART_Cmd(ESP8266_USART, ENABLE);
}

void ESP8266_SendRaw(const uint8_t *data, uint32_t len)
{
	for(uint32_t i = 0; i < len; i++)
	{
		ESP8266_SendByte(data[i]);
	}
}

//读到数据后会立刻返回，如果无数据则阻塞1ms
int ESP8266_ReadByte(uint8_t *data, uint32_t timeout_ms)
{
	TickType_t start = xTaskGetTickCount();
	TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

	if(timeout == 0)
	{
		timeout = 1;
	}

	while((xTaskGetTickCount() - start) < timeout)
	{
		if(USART_GetFlagStatus(ESP8266_USART, USART_FLAG_RXNE) == SET)
		{
			*data = (uint8_t)USART_ReceiveData(ESP8266_USART);
			return 1;
		}
		vTaskDelay(1);
	}

	return 0;
}

void ESP8266_Flush(uint32_t quiet_ms)
{
	uint8_t data;
	TickType_t last_rx = xTaskGetTickCount();
	TickType_t quiet_ticks = pdMS_TO_TICKS(quiet_ms);

	if(quiet_ticks == 0)
	{
		quiet_ticks = 1;
	}

	while((xTaskGetTickCount() - last_rx) < quiet_ticks)
	{
		if(USART_GetFlagStatus(ESP8266_USART, USART_FLAG_RXNE) == SET)
		{
			data = (uint8_t)USART_ReceiveData(ESP8266_USART);
			(void)data;
			last_rx = xTaskGetTickCount();
		}
		else
		{
			vTaskDelay(1);
		}
	}
}

int ESP8266_WaitForString(const char *expect, uint32_t timeout_ms)
{
	uint8_t data;
	uint32_t matched = 0;
	uint32_t expect_len = strlen(expect);
	TickType_t start = xTaskGetTickCount();
	TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

	if(expect == NULL || expect_len == 0)
	{
		return 0;
	}

	if(timeout == 0)
	{
		timeout = 1;
	}

	while((xTaskGetTickCount() - start) < timeout)
	{
		if(ESP8266_ReadByte(&data, 20))
		{
			if(data == (uint8_t)expect[matched])
			{
				matched++;
				if(matched >= expect_len)
				{
					return 0;
				}
			}
			else
			{
				matched = (data == (uint8_t)expect[0]) ? 1 : 0;
			}
		}
	}

	return -1;
}

int ESP8266_SendCommand(const char *cmd, const char *expect, uint32_t timeout_ms)
{
	if(cmd != NULL)
	{
		ESP8266_Flush(20);
		ESP8266_SendRaw((const uint8_t *)cmd, strlen(cmd));
		ESP8266_SendRaw((const uint8_t *)"\r\n", 2);
	}

	if(expect == NULL)
	{
		return 0;
	}

	return ESP8266_WaitForString(expect, timeout_ms);
}

int ESP8266_JoinAP(const char *ssid, const char *password, uint32_t timeout_ms)
{
	char cmd[160];

	if(ESP8266_SendCommand("ATE0", "OK", 1000) != 0)
	{
		return -1;
	}

	if(ESP8266_SendCommand("AT+CWMODE=1", "OK", 1000) != 0)
	{
		return -1;
	}

	snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
	if(ESP8266_SendCommand(cmd, "OK", timeout_ms) != 0)
	{
		return -1;
	}

	ESP8266_SendCommand("AT+CIPMUX=0", "OK", 1000);
	return 0;
}

int ESP8266_OpenTcp(const char *host, uint16_t port, uint32_t timeout_ms)
{
	char cmd[128];

	ESP8266_CloseTcp();
	snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
	return ESP8266_SendCommand(cmd, "OK", timeout_ms);
}

void ESP8266_CloseTcp(void)
{
	ESP8266_SendCommand("AT+CIPCLOSE", NULL, 0);
	vTaskDelay(pdMS_TO_TICKS(80));
	ESP8266_Flush(20);
}

int ESP8266_SendTcpData(const char *data, uint32_t len, uint32_t timeout_ms)
{
	char cmd[32];

	snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%lu", (unsigned long)len);
	if(ESP8266_SendCommand(cmd, ">", 2000) != 0)
	{
		return -1;
	}

	ESP8266_SendRaw((const uint8_t *)data, len);
	return ESP8266_WaitForString("SEND OK", timeout_ms);
}
