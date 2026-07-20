#ifndef __ONENET_OTA_H
#define __ONENET_OTA_H

#include "stm32f10x.h"

#define ONENET_OTA_ENABLE 1

#define ONENET_WIFI_SSID "YOUR_WIFI_SSID"
#define ONENET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define ONENET_OTA_HOST "iot-api.heclouds.com"
#define ONENET_OTA_PORT 80U
#define ONENET_PRODUCT_ID "YOUR_PRODUCT_ID"
#define ONENET_DEVICE_NAME "YOUR_DEVICE_NAME"
#define ONENET_AUTH_TOKEN "YOUR_ONENET_AUTHORIZATION_TOKEN"

#define ONENET_VERSION_PATH_FMT "/fuse-ota/%s/%s/version"
#define ONENET_CHECK_PATH_FMT "/fuse-ota/%s/%s/check?type=%lu&version=%s"
#define ONENET_STATUS_PATH_FMT "/fuse-ota/%s/%s/%lu/status"
#define ONENET_DOWNLOAD_PATH_FMT "/fuse-ota/%s/%s/%lu/download"

#define ONENET_APP_VERSION "V1.0"
#define ONENET_MODULE_VERSION "ESP8266_AT"
#define ONENET_OTA_TYPE 2U

#define ONENET_OTA_CHECK_INTERVAL_MS 600000U
#define ONENET_OTA_BOOT_DELAY_MS 5000U
#define ONENET_DOWNLOAD_CHUNK_SIZE 512U
#define ONENET_REPORT_PROGRESS_ENABLE 1

void OneNET_OTA_Task(void *pvParameters);

#endif
