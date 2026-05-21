#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sensors.h"
#include "app_config.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "portmacro.h"
#include "sensor_context.h"
#include "rtos_objects.h"
#include "stdbool.h"

typedef struct aht20_sample_t {
	float temperature;
	float humidity;
} aht20_sample_t;

bool read_aht20(i2c_master_dev_handle_t aht20_handle, aht20_sample_t *aht20_sample) {
    uint8_t success;
	uint8_t measure[3] = {0xAC, 0x33, 0x00}; 
	esp_err_t err; 
	err = i2c_master_transmit(aht20_handle, measure, 3, -1);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Error on calibration!");
		return false;
	}
	vTaskDelay(pdMS_TO_TICKS(80));
	uint8_t init = 0x71;
	err = i2c_master_transmit_receive(aht20_handle, &init, 1, &success, 1, -1);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Error measuring temperature/humidity!");
		return false;
	}
	if (!(success & (1 << 7))) {
		// measurement completed
		uint8_t data[6];
		err = i2c_master_receive(aht20_handle, data, 6, -1);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "Error measuring temperature/humidity!");
			return false;
		}
		// data[0] = status
		// data[1], data[2], upper 4 bits of data[3] = humidity
		// lower 4 bits of data[3], data[4], data[5] = temperature
		// remaining: CRC
		uint32_t humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)(data[3] >> 4) & 0x0F);
		uint32_t temperature = ((uint32_t)(data[3] & 0x0F) << 16) | (uint32_t)data[4] << 8 | (uint32_t)data[5];
		aht20_sample->humidity = 100.0f * humidity / (1048576.0f);
		aht20_sample->temperature = (200.0f * temperature / (1048576.0f)) - 50.0f;
		return true;
	}
	return false;
}

motion_state_t read_hcsr505(i2c_master_dev_handle_t hcsr505_handle) {
	uint8_t init[2] = {0xFF, 0xFF};  
	uint8_t raw_data[2];
	esp_err_t err;
	err = i2c_master_transmit(hcsr505_handle, init, 2, -1);
	if (err != ESP_OK) {
		return SENSOR_FAULT;
	}
	err = i2c_master_receive(hcsr505_handle, raw_data, 2, -1);
	if (err != ESP_OK) {
		return SENSOR_FAULT;
	}
	uint16_t data = ((uint16_t)(raw_data[1] << 8)) | raw_data[0];
	return (data & (uint16_t)(1 << HCSR505_GPIO))?  MOTION_DETECTED : MOTION_CLEAR;
}

// calculate the ppm from the voltage
// returns V
bool read_mq135(adc_oneshot_unit_handle_t adc_oneshot_handle, adc_cali_handle_t adc_cali_handle, float *voltage) {
	int adc_raw, adc_cali; 
	esp_err_t err; 
	err = adc_oneshot_read(adc_oneshot_handle, ADC_CHANNEL_1, &adc_raw);
	if (err != ESP_OK) {
		return false;
	}
	err = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &adc_cali);
	if (err != ESP_OK) {
		return false;
	}
	*voltage = adc_cali / 1000.0f;
	return true;
}


void motion_detected_isr(void* pvParams) {
	BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
	TaskHandle_t *xTaskToNotify = (TaskHandle_t*)pvParams;
	vTaskNotifyGiveFromISR(*xTaskToNotify, &pxHigherPriorityTaskWoken); 
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
}

void motion_detected_handler(void *pvParams) {
	while(1) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
		motion_state_t motion_state = read_hcsr505(pcf8575_handle); 
		if (motion_state == MOTION_DETECTED) {
			ESP_LOGW(S3_TAG, "Motion Detected!"); 
		}
	}
}


// Periodic application task that will eventually gather all sensor values and
// refresh the outgoing telemetry JSON. Right now the payload structure and
// synchronization are in place even though sensor collection is still being
// expanded.
void measure_sensor_values(void* pv_params) {
	aht20_sample_t aht20_sample;
	bool aht20, mq135;
	float air_quality;
	motion_state_t motion_state;
	while (1) {
		xEventGroupWaitBits(
			collect_group,
			MEASURE_ALL_BIT,
			pdTRUE, 
			pdTRUE,
			portMAX_DELAY
		);
		// AHT20
		aht20 = read_aht20(aht20_handle, &aht20_sample);
		// MQ135
		mq135 = read_mq135(adc_oneshot_handle, adc_cali_handle, &air_quality);	
		 // HCSR505
		motion_state = read_hcsr505(pcf8575_handle);
		if (mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
			sensor_data->temperature = aht20? aht20_sample.temperature : -1.0f;
			sensor_data->humidity = aht20? aht20_sample.humidity : -1.0f;
			sensor_data->air_quality = mq135? air_quality : -1.0f;
			sensor_data->motion_state = motion_state;
			xSemaphoreGive(mutex);
		}
	}	
}

