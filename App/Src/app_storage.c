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
#include <stdio.h>
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

				case CMD_BENCHMARK_REQ:
				{
				    FIL file;
				    UINT bytes_read, bytes_written;
				    uint32_t start_time, end_time, total_time_ms;
				    float transfer_rate_kbps = 0.0f;
				    FRESULT fr;

				    static UART_Packet_t bench_packet;
				    const uint32_t benchmark_size = 1024 * 1024; // 1 MegaByte
				    uint32_t total_bytes_processed = 0;

				    // open file
				    fr = f_open(&file, "bench.dat", FA_READ);
				    if (fr != FR_OK) {
				        fr = f_open(&file, "bench.dat", FA_CREATE_ALWAYS | FA_WRITE);
				        if (fr == FR_OK) {
				            memset(bench_packet.payload, 0x5A, 512);
				            for(uint32_t i = 0; i < 2048; i++) {
				                f_write(&file, bench_packet.payload, 512, &bytes_written);
				            }
				            f_close(&file);
				        }
				        fr = f_open(&file, "bench.dat", FA_READ);
				    }

				    if (fr != FR_OK) {
				        memset(&tx_packet, 0, sizeof(UART_Packet_t));
				        tx_packet.cmd = CMD_BENCHMARK_REQ + 0x80;
				        tx_packet.length = snprintf((char*)tx_packet.payload, MAX_PAYLOAD_SIZE, "FatFs Open Error: %d\r\n", fr);
				        xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
				        break;
				    }

				    bench_packet.cmd = CMD_BENCHMARK_DATA;
				    bench_packet.length = 512;

				    start_time = HAL_GetTick();

				    while (total_bytes_processed < benchmark_size) {
				        fr = f_read(&file, bench_packet.payload, 512, &bytes_read);
				        if (fr != FR_OK || bytes_read == 0) {
				            break;
				        }

				        xQueueSend(qStorageToUart, &bench_packet, portMAX_DELAY);
				        total_bytes_processed += bytes_read;
				    }

				    f_close(&file);
				    end_time = HAL_GetTick();

				    total_time_ms = end_time - start_time;
				    if (total_time_ms == 0) {
				        total_time_ms = 1;
				    }

				    transfer_rate_kbps = ((float)total_bytes_processed / 1024.0f) / ((float)total_time_ms / 1000.0f);

				    memset(&tx_packet, 0, sizeof(UART_Packet_t));
				    tx_packet.cmd = CMD_BENCHMARK_REQ + 0x80;
				    tx_packet.length = snprintf((char*)tx_packet.payload, MAX_PAYLOAD_SIZE,
				                              "\r\n--- BENCHMARK RESULT ---\r\nProcessed: %lu Bytes\r\nTotal Time: %lu ms\r\nSpeed: %.2f KB/s\r\nLast Fr Code: %d\r\n",
				                              total_bytes_processed, total_time_ms, transfer_rate_kbps, fr); // In thêm fr

				    xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
				    break;
				}
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
