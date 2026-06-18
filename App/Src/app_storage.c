/*
 * app_storage.c
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */
#include "app_storage.h"
#include "app_comm.h"
#include "app_freertos.h"
#include "fatfs.h"
#include <string.h>

void Task_Storage_Handler(void *pvParameters) {
    FATFS fs;
    UART_Packet_t rx_packet;
	UART_Packet_t tx_packet;

    // Tat LED
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);

    // Mount sd
    if (f_mount(&fs, "", 1) != FR_OK) {
        // Loi: Bat LED G14
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
    } else {
    	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);
    }

    while (1){
    	// Cho du lieu trong queue gui tu uart
    	if (xQueueReceive(qUartToStorage, &rx_packet, portMAX_DELAY) == pdPASS){
    		// reset packet
    		memset(&tx_packet, 0, sizeof(UART_Packet_t));

    		switch (rx_packet.cmd) {
				case CMD_SYS_PING_REQ:
					// TEST 1: PC ping STM32 -> STM32 Pong
					tx_packet.cmd = CMD_SYS_PING_ACK;
					tx_packet.length = 4;
					memcpy(tx_packet.payload, "PONG", 4);

					xQueueSend(qStorageToUart, &tx_packet, 0);
					break;

				case CMD_GET_SYS_INFO_REQ:
					// TEST 2: PC xin info -> tra string de test
					tx_packet.cmd = CMD_GET_SYS_INFO_ACK;
					char info_msg[] = "SD OK, 32GB Free";
					tx_packet.length = strlen(info_msg);
					memcpy(tx_packet.payload, info_msg, tx_packet.length);

					xQueueSend(qStorageToUart, &tx_packet, 0);
					break;


				case CMD_SYS_PING_ACK:
				case CMD_GET_SYS_INFO_ACK:
				case CMD_DATA_CHUNK_ACK:
				case CMD_ERROR_ACK:
					break;
				default:
					// Lenh khong ton tai, tra ma loi
					tx_packet.cmd = CMD_ERROR_ACK;
					tx_packet.length = 1;
					tx_packet.payload[0] = 0xEE;

					xQueueSend(qStorageToUart, &tx_packet, 0);
					break;
			}

    	}
    }
}
