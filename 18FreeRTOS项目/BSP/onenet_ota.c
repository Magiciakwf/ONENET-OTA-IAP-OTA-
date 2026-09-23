#include "onenet_ota.h"
#include "CAN_Tp.h"
#include "Serial.h"
#include "esp8266.h"
#include "ota.h"
#include "queue.h"
#include "start_task.h"
#include "task.h"
#include "user_flash.h"
#include "user_ledtask.h"
#include <stdio.h>
#include <string.h>

#if ONENET_DOWNLOAD_CHUNK_SIZE > CANTP_RECVSIZE
#error "ONENET_DOWNLOAD_CHUNK_SIZE must be <= CANTP_RECVSIZE"
#endif

#define ONENET_HTTP_REQUEST_SIZE 1024U
#define ONENET_HTTP_RESPONSE_SIZE 1024U
#define ONENET_HTTP_HEADER_SIZE 768U
#define ONENET_FLASH_RESULT_TIMEOUT_MS 30000U

typedef struct
{
	uint32_t tid;
	uint32_t size;
	char target[24];
	char md5[33];
} OneNET_OTA_Info_t;

typedef struct
{
	uint32_t remain;
} OneNET_IPD_Stream_t;

static char s_request[ONENET_HTTP_REQUEST_SIZE];
static char s_response[ONENET_HTTP_RESPONSE_SIZE];
static char s_header[ONENET_HTTP_HEADER_SIZE];
static uint8_t s_download_buf[ONENET_DOWNLOAD_CHUNK_SIZE];
static OTA_Msg_t s_ota_msg;

static int onenet_wait_flash_result(void)
{
	uint32_t result;

	if(xTaskNotifyWait(0,
					   0xFFFFFFFFUL,
					   &result,
					   pdMS_TO_TICKS(ONENET_FLASH_RESULT_TIMEOUT_MS)) != pdTRUE)
	{
		return -1;
	}

	return (result == OTA_STATUS_OK) ? 0 : -1;
}



static int onenet_json_get_uint(const char *json, const char *key, uint32_t *value)
{
	char pattern[32];
	const char *p;
	uint32_t result = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if(p == NULL)
	{
		return -1;
	}

	p = strchr(p, ':');
	if(p == NULL)
	{
		return -1;
	}

	p++;
	while(*p == ' ' || *p == '\"')
	{
		p++;
	}

	if(*p < '0' || *p > '9')
	{
		return -1;
	}

	while(*p >= '0' && *p <= '9')
	{
		result = result * 10 + (uint32_t)(*p - '0');
		p++;
	}

	*value = result;
	return 0;
}

static int onenet_json_get_string(const char *json, const char *key, char *out, uint32_t out_size)
{
	char pattern[32];
	const char *p;
	uint32_t i = 0;

	if(out_size == 0)
	{
		return -1;
	}

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if(p == NULL)
	{
		return -1;
	}

	p = strchr(p, ':');
	if(p == NULL)
	{
		return -1;
	}

	p++;
	while(*p == ' ')
	{
		p++;
	}

	if(*p != '\"')
	{
		return -1;
	}
	p++;

	while(*p != '\0' && *p != '\"' && i < (out_size - 1))
	{
		out[i++] = *p++;
	}
	out[i] = '\0';

	return (i > 0) ? 0 : -1;
}

static int onenet_ipd_read_byte(OneNET_IPD_Stream_t *stream, uint8_t *out, uint32_t timeout_ms)
{
	uint8_t ch;
	uint8_t state = 0;
	uint32_t len = 0;
	TickType_t start = xTaskGetTickCount();
	TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

	if(timeout == 0)
	{
		timeout = 1;
	}

	if(stream->remain > 0)
	{
		if(ESP8266_ReadByte(out, timeout_ms))
		{
			stream->remain--;
			return 1;
		}
		return 0;
	}

	while((xTaskGetTickCount() - start) < timeout)
	{
		if(ESP8266_ReadByte(&ch, 20) == 0)
		{
			continue;
		}

		switch(state)
		{
			case 0: state = (ch == '+') ? 1 : 0; break;
			case 1: state = (ch == 'I') ? 2 : ((ch == '+') ? 1 : 0); break;
			case 2: state = (ch == 'P') ? 3 : 0; break;
			case 3: state = (ch == 'D') ? 4 : 0; break;
			case 4: state = (ch == ',') ? 5 : 0; break;
			case 5:
				if(ch >= '0' && ch <= '9')
				{
					len = len * 10 + (uint32_t)(ch - '0');
				}
				else if(ch == ':')
				{
					stream->remain = len;
					if(stream->remain == 0)
					{
						return 0;
					}
					if(ESP8266_ReadByte(out, timeout_ms))
					{
						stream->remain--;
						return 1;
					}
					return 0;
				}
				else
				{
					state = 0;
					len = 0;
				}
				break;
			default:
				state = 0;
				break;
		}
	}

	return 0;
}

static int onenet_receive_http_text(char *out, uint32_t out_size, uint32_t timeout_ms)
{
	OneNET_IPD_Stream_t stream = {0};
	uint8_t ch;
	uint32_t len = 0;
	uint8_t got_data = 0;
	TickType_t start = xTaskGetTickCount();
	TickType_t last_data = start;
	TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

	if(out_size == 0)
	{
		return -1;
	}

	while((xTaskGetTickCount() - start) < timeout)
	{
		if(onenet_ipd_read_byte(&stream, &ch, 500))
		{
			got_data = 1;
			last_data = xTaskGetTickCount();
			if(len < (out_size - 1))
			{
				out[len++] = (char)ch;
			}
		}
		else if(got_data && (xTaskGetTickCount() - last_data) > pdMS_TO_TICKS(1000))
		{
			break;
		}
	}

	out[len] = '\0';
	return (len > 0) ? 0 : -1;
}

static int onenet_build_http_request(const char *method,
									 const char *path,
									 const char *extra_headers,
									 const char *body)
{
	uint32_t body_len = (body != NULL) ? strlen(body) : 0;
	int len;

	len = snprintf(s_request,
				   sizeof(s_request),
				   "%s %s HTTP/1.1\r\n"
				   "Host: %s\r\n"
				   "Authorization: %s\r\n"
				   "Content-Type: application/json\r\n"
				   "Connection: close\r\n"
				   "%s"
				   "%s"
				   "\r\n"
				   "%s",
				   method,
				   path,
				   ONENET_OTA_HOST,
				   ONENET_AUTH_TOKEN,
				   (body_len > 0) ? "" : "",
				   (extra_headers != NULL) ? extra_headers : "",
				   (body != NULL) ? body : "");

	if(len < 0 || (uint32_t)len >= sizeof(s_request))
	{
		return -1;
	}

	if(body_len > 0)
	{
		char content_len[40];
		snprintf(content_len, sizeof(content_len), "Content-Length: %lu\r\n", (unsigned long)body_len);
		len = snprintf(s_request,
					   sizeof(s_request),
					   "%s %s HTTP/1.1\r\n"
					   "Host: %s\r\n"
					   "Authorization: %s\r\n"
					   "Content-Type: application/json\r\n"
					   "Connection: close\r\n"
					   "%s"
					   "%s"
					   "\r\n"
					   "%s",
					   method,
					   path,
					   ONENET_OTA_HOST,
					   ONENET_AUTH_TOKEN,
					   content_len,
					   (extra_headers != NULL) ? extra_headers : "",
					   body);
		if(len < 0 || (uint32_t)len >= sizeof(s_request))
		{
			return -1;
		}
	}

	return len;
}

static int onenet_http_request(const char *method, const char *path, const char *body, char *response, uint32_t response_size)
{
	int req_len = onenet_build_http_request(method, path, NULL, body);
	if(req_len <= 0)
	{
		return -1;
	}

	if(ESP8266_OpenTcp(ONENET_OTA_HOST, ONENET_OTA_PORT, 8000) != 0)
	{
		return -1;
	}

	if(ESP8266_SendTcpData(s_request, (uint32_t)req_len, 5000) != 0)
	{
		ESP8266_CloseTcp();
		return -1;
	}

	if(onenet_receive_http_text(response, response_size, 8000) != 0)
	{
		ESP8266_CloseTcp();
		return -1;
	}

	ESP8266_CloseTcp();
	return 0;
}

static int onenet_response_success(const char *response)
{
	uint32_t code;

	if(onenet_json_get_uint(response, "code", &code) == 0 && code == 0)
	{
		return 1;
	}

	if(onenet_json_get_uint(response, "errno", &code) == 0 && code == 0)
	{
		return 1;
	}

	return 0;
}

static int onenet_report_version(void)
{
	char path[160];
	char body[96];

	snprintf(path,
			 sizeof(path),
			 ONENET_VERSION_PATH_FMT,
			 ONENET_PRODUCT_ID,
			 ONENET_DEVICE_NAME);
	snprintf(body,
			 sizeof(body),
			 "{\"s_version\":\"%s\",\"f_version\":\"%s\"}",
			 ONENET_APP_VERSION,
			 ONENET_MODULE_VERSION);

	memset(s_response, 0, sizeof(s_response));
	if(onenet_http_request("POST", path, body, s_response, sizeof(s_response)) != 0)
	{
		return -1;
	}

	return onenet_response_success(s_response) ? 0 : -1;
}

static int onenet_check_task(OneNET_OTA_Info_t *info)
{
	char path[192];
	uint32_t code;

	snprintf(path,
			 sizeof(path),
			 ONENET_CHECK_PATH_FMT,
			 ONENET_PRODUCT_ID,
			 ONENET_DEVICE_NAME,
			 (unsigned long)ONENET_OTA_TYPE,
			 ONENET_APP_VERSION);

	memset(s_response, 0, sizeof(s_response));
	if(onenet_http_request("GET", path, NULL, s_response, sizeof(s_response)) != 0)
	{
		return -1;
	}

	if(onenet_json_get_uint(s_response, "code", &code) == 0 && code == 12012)
	{
		return 1;
	}

	if(onenet_response_success(s_response) == 0)
	{
		return -1;
	}

	memset(info, 0, sizeof(*info));
	if(onenet_json_get_uint(s_response, "tid", &info->tid) != 0 ||
	   onenet_json_get_uint(s_response, "size", &info->size) != 0)
	{
		return -1;
	}

	onenet_json_get_string(s_response, "target", info->target, sizeof(info->target));
	onenet_json_get_string(s_response, "md5", info->md5, sizeof(info->md5));

	return 0;
}

static void onenet_report_status(uint32_t tid, uint16_t step)
{
#if ONENET_REPORT_PROGRESS_ENABLE
	char path[192];
	char body[24];

	snprintf(path,
			 sizeof(path),
			 ONENET_STATUS_PATH_FMT,
			 ONENET_PRODUCT_ID,
			 ONENET_DEVICE_NAME,
			 (unsigned long)tid);
	snprintf(body, sizeof(body), "{\"step\":%u}", step);
	(void)onenet_http_request("POST", path, body, s_response, sizeof(s_response));
#else
	(void)tid;
	(void)step;
#endif
}

static int onenet_queue_start(uint32_t file_size)
{
	memset(&s_ota_msg, 0, sizeof(s_ota_msg));
	s_ota_msg.type = START;
	s_ota_msg.notify_task = xTaskGetCurrentTaskHandle();//告诉Flash任务通知的对象是这个OTA任务
	s_ota_msg.data.file_size = file_size;
	xTaskNotifyStateClear(NULL);//清除任务通知标志位
	return (xQueueSend(OTA_Queue, &s_ota_msg, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

static int onenet_queue_data(const uint8_t *data, uint32_t len)
{
	if(len > CANTP_RECVSIZE)
	{
		return -1;
	}

	memset(&s_ota_msg, 0, sizeof(s_ota_msg));
	s_ota_msg.type = DATA;
	s_ota_msg.notify_task = xTaskGetCurrentTaskHandle();
	s_ota_msg.Recv_len = len;
	memcpy(s_ota_msg.data.CANTP_RecvBuf, data, len);
	xTaskNotifyStateClear(NULL);
	return (xQueueSend(OTA_Queue, &s_ota_msg, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

static int onenet_queue_end(uint32_t expected_crc)
{
	memset(&s_ota_msg, 0, sizeof(s_ota_msg));
	s_ota_msg.type = END;
	s_ota_msg.notify_task = xTaskGetCurrentTaskHandle();
	s_ota_msg.data.crc_buf[0] = (uint8_t)(expected_crc >> 24);
	s_ota_msg.data.crc_buf[1] = (uint8_t)(expected_crc >> 16);
	s_ota_msg.data.crc_buf[2] = (uint8_t)(expected_crc >> 8);
	s_ota_msg.data.crc_buf[3] = (uint8_t)expected_crc;
	xTaskNotifyStateClear(NULL);
	return (xQueueSend(OTA_Queue, &s_ota_msg, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

static int onenet_http_status_is_valid(const char *header, uint32_t offset, uint32_t expected_len, uint32_t total_size)
{
	if(strstr(header, " 206 ") != NULL)
	{
		return 1;
	}

	if(offset == 0 && expected_len == total_size && strstr(header, " 200 ") != NULL)
	{
		return 1;
	}

	return 0;
}

static int onenet_download_range(const OneNET_OTA_Info_t *info,
								 uint32_t offset,
								 uint32_t expected_len,
								 uint32_t *crc)
{
	OneNET_IPD_Stream_t stream = {0};
	char path[192];
	char range_header[64];
	uint32_t header_len = 0;
	uint32_t body_len = 0;
	uint8_t header_done = 0;
	uint8_t match = 0;
	uint8_t ch;
	int req_len;
	TickType_t start;

	snprintf(path,
			 sizeof(path),
			 ONENET_DOWNLOAD_PATH_FMT,
			 ONENET_PRODUCT_ID,
			 ONENET_DEVICE_NAME,
			 (unsigned long)info->tid);
	snprintf(range_header,
			 sizeof(range_header),
			 "Range: bytes=%lu-%lu\r\n",
			 (unsigned long)offset,
			 (unsigned long)(offset + expected_len - 1));

	req_len = onenet_build_http_request("GET", path, range_header, NULL);
	if(req_len <= 0)
	{
		return -1;
	}

	if(ESP8266_OpenTcp(ONENET_OTA_HOST, ONENET_OTA_PORT, 8000) != 0)
	{
		return -1;
	}

	if(ESP8266_SendTcpData(s_request, (uint32_t)req_len, 5000) != 0)
	{
		ESP8266_CloseTcp();
		return -1;
	}

	memset(s_header, 0, sizeof(s_header));
	start = xTaskGetTickCount();

	while((xTaskGetTickCount() - start) < pdMS_TO_TICKS(15000))
	{
		//逐字节接收数据
		if(onenet_ipd_read_byte(&stream, &ch, 1000) == 0)
		{
			continue;
		}

		if(header_done == 0)
		{
			if(header_len < (sizeof(s_header) - 1))
			{
				s_header[header_len++] = (char)ch;
				s_header[header_len] = '\0';
			}
			else
			{
				ESP8266_CloseTcp();
				return -1;
			}

			if((match == 0 && ch == '\r') ||
			   (match == 1 && ch == '\n') ||
			   (match == 2 && ch == '\r') ||
			   (match == 3 && ch == '\n'))
			{
				match++;
				if(match == 4)
				{
					header_done = 1;
					if(onenet_http_status_is_valid(s_header, offset, expected_len, info->size) == 0)
					{
						ESP8266_CloseTcp();
						return -1;
					}
				}
			}
			else
			{
				match = (ch == '\r') ? 1 : 0;
			}
		}
		else
		{
			//把最终数据写入数组
			if(body_len < expected_len)
			{
				s_download_buf[body_len++] = ch;
				if(body_len == expected_len)
				{
					break;
				}
			}
		}
	}

	ESP8266_CloseTcp();

	if(body_len != expected_len)
	{
		return -1;
	}
	//计算CRC，拷贝到结构体里再塞进数组里
	*crc = OTA_CRC32_Update(*crc, s_download_buf, body_len);
	//把含数组的结构体塞进队列
	return onenet_queue_data(s_download_buf, body_len);
}

static int onenet_download_file(const OneNET_OTA_Info_t *info)
{
	uint32_t offset = 0;
	uint32_t crc = OTA_CRC32_Init();
	uint8_t next_progress = 10;

	Led_SetOtaState(LED_OTA_DOWNLOAD); /* 新一轮升级清除上一次 OTA 故障灯效 */

	if(info->size == 0 || info->size > USER_APP_MAX_SIZE)
	{
		Serial_Printf("OneNET OTA size invalid:%lu\r\n", (unsigned long)info->size);
		return -1;
	}

	//拼接START的结构体，所有任务通知最多等待30s
	if(onenet_queue_start(info->size) != 0 ||
	   onenet_wait_flash_result() != 0)
	{
		return -1;
	}

	//开始上报升级进度
	onenet_report_status(info->tid, 1);

	//offset表示成功下载的字节数
	//info->size表示总任务字节数
	//当数据全部塞入队列后才会退出循环
	while(offset < info->size)
	{
		//确定剩下多少字节没下载
		uint32_t remain = info->size - offset;
		//决定此次的下载量（最大为512字节）
		uint32_t part_len = (remain > ONENET_DOWNLOAD_CHUNK_SIZE) ? ONENET_DOWNLOAD_CHUNK_SIZE : remain;
		uint8_t progress;
		//建立TCP连接
		//发送带Range的HTTP 请求
		//读取part_len字节的ESP8266的网络数据到s_download_buf
		//更新下载过程中的CRC
		if(onenet_download_range(info, offset, part_len, &crc) != 0 ||
		   onenet_wait_flash_result() != 0)//每512个字节任务通知就阻塞一次
		{
			onenet_report_status(info->tid, 107);
			return -1;
		}
		// 已等待 Flash 完成通知，因此这里表示本块确实写入成功。

		offset += part_len;
		progress = (uint8_t)((offset * 100U) / info->size);
		if(progress >= next_progress)
		{
			onenet_report_status(info->tid, progress);
			while(next_progress <= progress && next_progress < 100)
			{
				next_progress += 10;
			}
		}
	}

	onenet_report_status(info->tid, 100);
	crc = OTA_CRC32_Finish(crc);
	if(onenet_queue_end(crc) != 0 ||
	   onenet_wait_flash_result() != 0)
	{
		onenet_report_status(info->tid, 107);
		return -1;
	}

	onenet_report_status(info->tid, 101);
	return 0;
}

void OneNET_OTA_Task(void *pvParameters)
{
	OneNET_OTA_Info_t info;

	(void)pvParameters;
	/* 这里显示最近一次联网结果，不额外启动网络轮询任务。 */
	Led_SetNetwork(ONENET_OTA_ENABLE ? 0 : 1);
	vTaskDelay(pdMS_TO_TICKS(ONENET_OTA_BOOT_DELAY_MS));

#if ONENET_OTA_ENABLE

	// ESP8266_Init(ESP8266_DEFAULT_BAUDRATE);

	while(1)
	{
		if(ESP8266_JoinAP(ONENET_WIFI_SSID, ONENET_WIFI_PASSWORD, 15000) != 0)
		{
			Led_SetNetwork(0);
			Serial_Printf("ESP8266 WiFi join failed\r\n");
			vTaskDelay(pdMS_TO_TICKS(30000));
			continue;
		}

		if(onenet_report_version() != 0)
		{
			Led_SetNetwork(0);
			Serial_Printf("OneNET report version failed\r\n");
			vTaskDelay(pdMS_TO_TICKS(ONENET_OTA_CHECK_INTERVAL_MS));
			continue;
		}

		Led_SetNetwork(1);
		switch(onenet_check_task(&info))
		{
			case 0:
				Serial_Printf("OneNET OTA found tid:%lu size:%lu target:%s\r\n",
							  (unsigned long)info.tid,
							  (unsigned long)info.size,
							  info.target);
				if(onenet_download_file(&info) != 0)
				{
					Led_SetOtaState(LED_OTA_ERROR);
					Serial_Printf("OneNET OTA download failed\r\n");
				}
				break;

			case 1:
				Serial_Printf("OneNET OTA no task\r\n");
				break;

			default:
				Led_SetNetwork(0);
				Serial_Printf("OneNET OTA check failed\r\n");
				break;
		}

		vTaskDelay(pdMS_TO_TICKS(ONENET_OTA_CHECK_INTERVAL_MS));//每10分钟执行一次
	}
#else
	vTaskDelete(NULL);
	return;
#endif
}
