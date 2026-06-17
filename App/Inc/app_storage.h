/*
 * app_storage.h
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */

#ifndef INC_APP_STORAGE_H_
#define INC_APP_STORAGE_H_

#include "FreeRTOS.h"
#include "task.h"
#include "fatfs.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define BENCH_FILE_SIZE 1048576
#define BENCH_CHUNK_SIZE 4096


void Task_Storage_Handler(void *pvParameters);

#endif /* INC_APP_STORAGE_H_ */
