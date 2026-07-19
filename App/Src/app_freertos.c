/*
 * app_freertos.c
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */
#include "app_freertos.h"
#include "app_storage.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

TaskHandle_t hStorageTask;
<<<<<<< HEAD
=======
TaskHandle_t hCommTask;
TaskHandle_t hUIReq;

//2 Queue cho 2 chieu gui du lieu
>>>>>>> refs/heads/add_error_handle
QueueHandle_t qUartToStorage;
<<<<<<< HEAD
=======
QueueHandle_t qStorageToUart;
QueueHandle_t qUIReq;

// Semaphore
SemaphoreHandle_t semUartTx;
>>>>>>> refs/heads/add_error_handle


void App_FreeRTOS_Init(void) {
<<<<<<< HEAD
    qUartToStorage = xQueueCreate(10, sizeof(uint32_t));
=======
	// Khoi tao queue
	qUartToStorage = xQueueCreate(5, sizeof(UART_Packet_t));	// Chua toi da 5 lenh gui tu PC
	qStorageToUart = xQueueCreate(5, sizeof(UART_Packet_t));	// Chua 5 block data gui tu sd
	qUIReq         = xQueueCreate(2, sizeof(UART_Packet_t));


>>>>>>> refs/heads/add_error_handle

<<<<<<< HEAD
    xTaskCreate(Task_Storage_Handler, "Storage", 1024, NULL, 3, &hStorageTask);
    // xTaskCreate(Task_Comm_Handler, "Comm", 512, NULL, 2, &hCommTask);
=======
    xTaskCreate(Task_Storage_Handler, "Storage", 2048 , NULL, 40, &hStorageTask);
    xTaskCreate(Task_Comm_Handler, "Comm", 1024, NULL, 39, &hCommTask);
    xTaskCreate(Task_UIReq_Handler, "UIReq", 1024, NULL, 41, &hUIReq);

    semUartTx = xSemaphoreCreateBinary();
>>>>>>> refs/heads/add_error_handle
}
