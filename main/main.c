#include "s3.h"

void app_main() {   
	// change s3_ota_update to a task scheduled by FreeRTOS later
	// sensor readings and other tasks should also be scheduled
	s3_main();
}