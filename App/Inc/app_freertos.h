/*
 * app_freertos.h
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */

#ifndef INC_APP_FREERTOS_H_
#define INC_APP_FREERTOS_H_

<<<<<<< HEAD
=======
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

// Khai bao cac Handler
extern QueueHandle_t qUartToStorage;
extern QueueHandle_t qStorageToUart;
extern QueueHandle_t qUIReq;
extern SemaphoreHandle_t semUartTx;

>>>>>>> refs/heads/add_error_handle
void App_FreeRTOS_Init(void);

#endif /* INC_APP_FREERTOS_H_ */
