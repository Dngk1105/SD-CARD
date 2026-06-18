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

//void Task_Fake_Injector(void *pvParameters) {
//    UART_Packet_t fake_rx;
//    fake_rx.cmd = 0x00; // CMD_SYS_PING_REQ
//    fake_rx.length = 0;
//
//    // Đợi OS khởi động ổn định
//    vTaskDelay(pdMS_TO_TICKS(2000));
//
//    while(1) {
//        // Bơm 1 lệnh Ping vào Queue
//        xQueueSend(qUartToStorage, &fake_rx, portMAX_DELAY);
//
//        // Ngủ đông 5 giây rồi bơm tiếp
//        vTaskDelay(pdMS_TO_TICKS(5000));
//    }
//}

void App_FreeRTOS_Init(void) {
	// Khoi tao queue
	qUartToStorage = xQueueCreate(5, sizeof(UART_Packet_t));	// Chua toi da 5 lenh gui tu PC
	qStorageToUart = xQueueCreate(2, sizeof(UART_Packet_t));	// Chua 2 block data gui tu sd



    xTaskCreate(Task_Storage_Handler, "Storage", 1024, NULL, 3, &hStorageTask);
    xTaskCreate(Task_Comm_Handler, "Comm", 512, NULL, 2, &hCommTask);

    semUartTx = xSemaphoreCreateBinary();
}
