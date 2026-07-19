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
TaskHandle_t hUIReq;

//2 Queue cho 2 chieu gui du lieu
QueueHandle_t qUartToStorage;
QueueHandle_t qStorageToUart;
QueueHandle_t qUIReq;

// Semaphore
SemaphoreHandle_t semUartTx;


void App_FreeRTOS_Init(void) {
	// Khoi tao queue
	qUartToStorage = xQueueCreate(5, sizeof(UART_Packet_t));	// Chua toi da 5 lenh gui tu PC
	qStorageToUart = xQueueCreate(5, sizeof(UART_Packet_t));	// Chua 5 block data gui tu sd
	qUIReq         = xQueueCreate(2, sizeof(UART_Packet_t));



    xTaskCreate(Task_Storage_Handler, "Storage", 2048 , NULL, 40, &hStorageTask);
    xTaskCreate(Task_Comm_Handler, "Comm", 1024, NULL, 39, &hCommTask);
    xTaskCreate(Task_UIReq_Handler, "UIReq", 1024, NULL, 41, &hUIReq);

    semUartTx = xSemaphoreCreateBinary();
}
