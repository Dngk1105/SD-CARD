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
QueueHandle_t qUartToStorage;

void App_FreeRTOS_Init(void) {
    qUartToStorage = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(Task_Storage_Handler, "Storage", 1024, NULL, 3, &hStorageTask);
    // xTaskCreate(Task_Comm_Handler, "Comm", 512, NULL, 2, &hCommTask);
}
