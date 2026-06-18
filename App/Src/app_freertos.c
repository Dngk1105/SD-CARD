/*
 * app_freertos.c
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */
#include "app_freertos.h"
#include "app_storage.h"
#include "app_comm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// Handler cho os
TaskHandle_t hStorageTask;
TaskHandle_t hCommTask;

//2 Queue cho 2 chieu gui du lieu
QueueHandle_t qUartToStorage;
QueueHandle_t qStorageToUart;

// Semaphore
SemaphoreHandle_t semUartTx;


void App_FreeRTOS_Init(void) {
	// Khoi tao queue
	qUartToStorage = xQueueCreate(5, sizeof(UART_Packet_t));	// Chua toi da 5 lenh gui tu PC
	qStorageToUart = xQueueCreate(8, sizeof(UART_Packet_t));	// Chua 8 block data gui tu sd



    xTaskCreate(Task_Storage_Handler, "Storage", 1024, NULL, 3, &hStorageTask);
    xTaskCreate(Task_Comm_Handler, "Comm", 512, NULL, 2, &hCommTask);

    semUartTx = xSemaphoreCreateBinary();
}
