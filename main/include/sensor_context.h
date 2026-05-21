#ifndef SENSOR_CONTEXT_H
#define SENSOR_CONTEXT_H

#include "lwip/sockets.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "freertos/semphr.h"

typedef enum motion_state_t {
	SENSOR_FAULT = -1,
	MOTION_DETECTED = 0,
	MOTION_CLEAR = 1,
} motion_state_t;

typedef struct stream_data {
	char chip[16];
	char firmvers[16];
	char ip[16];
	float temperature;
	float humidity;
	float air_quality;
	motion_state_t motion_state;
} stream_data; 

// Transport-side context for the UDP streaming task. Bundles the latest
// payload string, destination address, and socket handle.
typedef struct stream_payload {
	stream_data* _stream_data;
	int sock;
	struct sockaddr_in dest_addr;
	char msg[512];
} stream_payload;

extern stream_data* sensor_data;
extern stream_payload* payload;
extern adc_oneshot_unit_handle_t adc_oneshot_handle; 
extern adc_cali_handle_t adc_cali_handle; 
extern i2c_master_dev_handle_t aht20_handle, pcf8575_handle;

#endif