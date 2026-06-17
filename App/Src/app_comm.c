/*
 * app_comm.c
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */

#include "app_comm.h"
#include "app_freertos.h"
#include "main.h"
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"


// USART 1
extern UART_HandleTypeDef huart1;

// Cau hinh buffer DMA
#define RX_DMA_BUF_SIZE 1024
static uint8_t rx_dma_buf[RX_DMA_BUF_SIZE];
static uint16_t old_pos = 0;

// Parse du lieu
static ParserState_t rx_state = STATE_WAIT_SYNC1;
static UART_Packet_t rx_packet;
static uint16_t payload_idx = 0;
static uint8_t calc_checksum = 0;
void App_Comm_ParseByte(uint8_t byte){
	switch (rx_state){
		case STATE_WAIT_SYNC1:
			if (byte == FRAME_SYNC1) rx_state = STATE_WAIT_SYNC2;
			break;
		case STATE_WAIT_SYNC2:
			if (byte == FRAME_SYNC2) rx_state = STATE_WAIT_CMD;
			else rx_state = STATE_WAIT_SYNC1;
			break;
		case STATE_WAIT_CMD:
			rx_packet.cmd = byte;
			calc_checksum = byte; // Bắt đầu tính Checksum
			rx_state = STATE_WAIT_LEN_L;
			break;
		case STATE_WAIT_LEN_L:
			rx_packet.length = byte;
			calc_checksum ^= byte;
			rx_state = STATE_WAIT_LEN_H;
			break;
		case STATE_WAIT_LEN_H:
			rx_packet.length |= (byte << 8);
			calc_checksum ^= byte;
			if (rx_packet.length > MAX_PAYLOAD_SIZE) {
				rx_state = STATE_WAIT_SYNC1; // Tràn bộ đệm, hủy frame
			} else if (rx_packet.length == 0) {
				rx_state = STATE_WAIT_CHECKSUM; // Command khong co payload
			} else {
				payload_idx = 0;
				rx_state = STATE_WAIT_PAYLOAD;
			}
			break;
		case STATE_WAIT_PAYLOAD:
			rx_packet.payload[payload_idx++] = byte;
			calc_checksum ^= byte;

			if (payload_idx >= rx_packet.length) {
				rx_state = STATE_WAIT_CHECKSUM;
			}
			break;
		case STATE_WAIT_CHECKSUM:
			if (byte == calc_checksum) {
				rx_state = STATE_WAIT_TAIL;
			} else {
				rx_state = STATE_WAIT_SYNC1; // Sai Checksum, hủy frame
			}
			break;
		case STATE_WAIT_TAIL:
			if (byte == FRAME_TAIL) {
				// Hop le day vao queue cho Storage_Task
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xQueueSendFromISR(qUartToStorage, &rx_packet, &xHigherPriorityTaskWoken);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
			rx_state = STATE_WAIT_SYNC1; // Reset để đón khung mới
			break;
		default:
			rx_state = STATE_WAIT_SYNC1;
			break;
	}
}

// Goi ngat khi bo dem dma day du lieu
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        uint16_t curr_pos = Size;
        uint16_t i;

        /* Mang bi ghi de kieu circular can kiem tra pos moi va cu */
        if (curr_pos != old_pos) {
            if (curr_pos > old_pos) {
            	// Xu li binh thuong
                for (i = old_pos; i < curr_pos; i++) {
                    App_Comm_ParseByte(rx_dma_buf[i]);
                }
            } else {
                // Neu bi cuon
                for (i = old_pos; i < RX_DMA_BUF_SIZE; i++) {
                    App_Comm_ParseByte(rx_dma_buf[i]);
                }
                for (i = 0; i < curr_pos; i++) {
                    App_Comm_ParseByte(rx_dma_buf[i]);
                }
            }
            old_pos = curr_pos;
        }
    }
}




// Xu li transfer len PC
static uint8_t tx_dma_buf[MAX_PAYLOAD_SIZE + 7]; // chua frame hoan chinh
void App_Comm_SendFrame(CommandID_t cmd, uint8_t *payload, uint16_t len) {
    uint8_t checksum = 0;
    uint16_t frame_idx = 0;

    //Start
    tx_dma_buf[frame_idx++] = FRAME_SYNC1;
    tx_dma_buf[frame_idx++] = FRAME_SYNC2;

    //Cmd
    tx_dma_buf[frame_idx++] = cmd;
    checksum = cmd;

    tx_dma_buf[frame_idx++] = (uint8_t)(len & 0xFF);        // LEN Low
    checksum ^= (uint8_t)(len & 0xFF);
    tx_dma_buf[frame_idx++] = (uint8_t)((len >> 8) & 0xFF); // LEN High
    checksum ^= (uint8_t)((len >> 8) & 0xFF);

    // Copy Payload
    if (len > 0 && payload != NULL) {
        memcpy(&tx_dma_buf[frame_idx], payload, len);
        for (uint16_t i = 0; i < len; i++) {
            checksum ^= payload[i];
        }
        frame_idx += len;
    }

    // Checksum và Tail
    tx_dma_buf[frame_idx++] = checksum;
    tx_dma_buf[frame_idx++] = FRAME_TAIL;


    xSemaphoreTake(semUartTx, 0);
    HAL_UART_Transmit_DMA(&huart1, tx_dma_buf, frame_idx);
    // Task ngu, cho den khi dma goi ngat
    xSemaphoreTake(semUartTx, portMAX_DELAY);
}

// Khi truyen xong du lieu, goi ngat de nha semaphore
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Nha semaphore
        xSemaphoreGiveFromISR(semUartTx, &xHigherPriorityTaskWoken);

        // Chuyen ngu canh lap tuc
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void Task_Comm_Handler(void *pvParameters) {
    uint8_t tx_payload[MAX_PAYLOAD_SIZE]; // Payload tu sd

    App_Comm_Init();

    while(1) {
        // Cho cho den khi storage task nhem data vao
        if (xQueueReceive(qStorageToUart, tx_payload, portMAX_DELAY) == pdPASS) {
        	// Truyen frame
        	App_Comm_SendFrame(CMD_DATA_CHUNK, tx_payload, 512);;
        }
    }
}





void App_Comm_Init(void) {
    rx_state = STATE_WAIT_SYNC1;
    memset(&rx_packet, 0, sizeof(UART_Packet_t));
    old_pos = 0;

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_dma_buf, RX_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT); // Tat Half Tranfer interupt


}

