#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "nvs_flash.h"

#include "iothub_client.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "iothubtransportmqtt.h"
#include "iothub_client_options.h"

// 连接字符串
static const char* connectionString = "HostName=LynkyPush.azure-devices.net;DeviceId=[REQUEST_IN_LOCAL];SharedAccessKey=[REQUEST_IN_LOCAL]";
// wifi用户名密码
#define EXAMPLE_WIFI_SSID "[REQUEST_IN_LOCAL]"
#define EXAMPLE_WIFI_PASS "[REQUEST_IN_LOCAL]"
// RTOS事件循环
static EventGroupHandle_t wifi_event_group;

const int CONNECTED_BIT = BIT0;
// 日志TAG
static const char *TAG = "[Azure]";
// wifi事件循环回调
static esp_err_t event_handler(void *ctx, system_event_t *event) {
	switch (event->event_id) {
	case SYSTEM_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		esp_wifi_connect();
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		break;
	default:
		break;
	}
	return ESP_OK;
}
// 初始化Wifi连接
static void initialise_wifi(void) {
	wifi_event_group = xEventGroupCreate();
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT()
	;
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	wifi_config_t wifi_config = { .sta = { .ssid = EXAMPLE_WIFI_SSID,
			.password = EXAMPLE_WIFI_PASS, }, };
	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...",
			wifi_config.sta.ssid);
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", EXAMPLE_WIFI_SSID,
			EXAMPLE_WIFI_PASS);
}
// azure iot 消息回调
static IOTHUBMESSAGE_DISPOSITION_RESULT receive_msg_callback(
		IOTHUB_MESSAGE_HANDLE message, void* user_context) {
	(void) user_context;
	const char* messageId;
	const char* correlationId;

	if ((messageId = IoTHubMessage_GetMessageId(message)) == NULL) {
		messageId = "<unavailable>";
	}

	if ((correlationId = IoTHubMessage_GetCorrelationId(message)) == NULL) {
		correlationId = "<unavailable>";
	}

	IOTHUBMESSAGE_CONTENT_TYPE content_type = IoTHubMessage_GetContentType(
			message);
	if (content_type == IOTHUBMESSAGE_BYTEARRAY) {
		const unsigned char* buff_msg;
		size_t buff_len;

		if (IoTHubMessage_GetByteArray(message, &buff_msg, &buff_len)
				!= IOTHUB_MESSAGE_OK) {
			(void) printf("Failure retrieving byte array message\r\n");
		} else {
			(void) printf(
					"Received Binary message\r\nMessage ID: %s\r\n Correlation ID: %s\r\n Data: <<<%.*s>>> & Size=%d\r\n",
					messageId, correlationId, (int) buff_len, buff_msg,
					(int) buff_len);
		}
	} else {
		const char* string_msg = IoTHubMessage_GetString(message);
		if (string_msg == NULL) {
			(void) printf("Failure retrieving byte array message\r\n");
		} else {
			(void) printf(
					"Received String Message\r\nMessage ID: %s\r\n Correlation ID: %s\r\n Data: <<<%s>>>\r\n",
					messageId, correlationId, string_msg);
		}
	}
	return IOTHUBMESSAGE_ACCEPTED;
}
// azure 任务(类似多线程)
void azure_task(void *pvParameter) {
	ESP_LOGI(TAG, "Waiting for WiFi access point ...");
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
	false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connected to access point success");
	(void) printf("Creating IoTHub handle\r\n");
	// - iot 核心
	size_t messages_count = 0;
	(void) platform_init();
	IOTHUB_CLIENT_LL_HANDLE iothub_ll_handle =
			IoTHubClient_LL_CreateFromConnectionString(connectionString,
					MQTT_Protocol);
	if (IoTHubClient_LL_SetMessageCallback(iothub_ll_handle,
			receive_msg_callback, &messages_count) != IOTHUB_CLIENT_OK) {
		(void) printf(
				"ERROR: IoTHubClient_LL_SetMessageCallback..........FAILED!\r\n");
	} else {
		do {
			IoTHubClient_LL_DoWork(iothub_ll_handle);
			vTaskDelay(10);

		} while (true);
	}
	IoTHubClient_LL_Destroy(iothub_ll_handle);
	platform_deinit();
	//
}

// 入口函数
void app_main() {
	nvs_flash_init();
	initialise_wifi();
	xTaskCreate(&azure_task, "azure_task", 8192, NULL, 5, NULL);
}
