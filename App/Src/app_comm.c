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
#define RX_DMA_BUF_SIZE (MAX_PAYLOAD_SIZE * 2)
static uint8_t rx_dma_buf[RX_DMA_BUF_SIZE];
static uint8_t tx_dma_buf[MAX_PAYLOAD_SIZE + 16]; // chua frame hoan chinh
static uint16_t old_pos = 0;

// Parse du lieu
static ParserState_t rx_state = STATE_WAIT_HEADER_H;
static UART_Packet_t rx_packet;
static uint16_t frame_len = 0;
static uint16_t payload_idx = 0;
static uint16_t calc_checksum = 0;
static uint16_t rcv_checksum = 0;

void App_Comm_ParseByte(uint8_t byte){
	switch (rx_state){
		case STATE_WAIT_HEADER_H:
			if (byte == FRAME_HEADER_H) rx_state = STATE_WAIT_HEADER_L;
			break;
		case STATE_WAIT_HEADER_L:
			if (byte == FRAME_HEADER_L) rx_state = STATE_WAIT_ADDR_0;
			else rx_state = STATE_WAIT_HEADER_H;
			break;

		// Bo qua 4 byte dia chi
		case STATE_WAIT_ADDR_0: rx_state = STATE_WAIT_ADDR_1; break;
		case STATE_WAIT_ADDR_1: rx_state = STATE_WAIT_ADDR_2; break;
		case STATE_WAIT_ADDR_2: rx_state = STATE_WAIT_ADDR_3; break;
		case STATE_WAIT_ADDR_3: rx_state = STATE_WAIT_PID; break;


		case STATE_WAIT_PID:
			rx_packet.packet_id = byte;
			calc_checksum = byte; // Bắt đầu tính Checksum
			rx_state = STATE_WAIT_LEN_H;
			break;
		case STATE_WAIT_LEN_H:
			frame_len = (byte << 8);
			calc_checksum += byte;
			rx_state = STATE_WAIT_LEN_L;
			break;
		case STATE_WAIT_LEN_L:
			frame_len |= byte;
			calc_checksum += byte;

			//frame length = cmd + payload + checksum
			if (frame_len < 3 || frame_len > MAX_PAYLOAD_SIZE + 3){
				rx_state = STATE_WAIT_HEADER_H; // Loi do dai
			} else {
				rx_packet.length = frame_len - 3;
				rx_state = STATE_WAIT_CMD;
			}
			break;
		case STATE_WAIT_CMD:
			rx_packet.cmd = byte;
			calc_checksum += byte;

			if (rx_packet.length == 0) rx_state = STATE_WAIT_CS_H;
			else {
				rx_state = STATE_WAIT_PAYLOAD;
				payload_idx = 0;
			}
			break;
		case STATE_WAIT_PAYLOAD:
			rx_packet.payload[payload_idx++] = byte;
			calc_checksum += byte;

			if (payload_idx >= rx_packet.length) rx_state = STATE_WAIT_CS_H;
			break;
		case STATE_WAIT_CS_H:
			rcv_checksum = (byte << 8);
			rx_state = STATE_WAIT_CS_L;
			break;
		case STATE_WAIT_CS_L:
			rcv_checksum |= byte;
			if (rcv_checksum == calc_checksum){
				// Hop le day vao queue cho Storage_Task
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xQueueSendFromISR(qUartToStorage, &rx_packet, &xHigherPriorityTaskWoken);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}

			rx_state = STATE_WAIT_HEADER_H; // Reset để đón khung mới
			break;
		default:
			rx_state = STATE_WAIT_HEADER_H;
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
void App_Comm_SendFrame(uint8_t packet_id, CommandID_t cmd, uint8_t *payload, uint16_t payload_len) {
    uint16_t checksum = 0;
    uint16_t frame_idx = 0;
    uint16_t len = payload_len + 3;

    //Start
    tx_dma_buf[frame_idx++] = FRAME_HEADER_H;
    tx_dma_buf[frame_idx++] = FRAME_HEADER_L;

    //ADDR
    tx_dma_buf[frame_idx++] = 0xFF;
	tx_dma_buf[frame_idx++] = 0xFF;
	tx_dma_buf[frame_idx++] = 0xFF;
	tx_dma_buf[frame_idx++] = 0xFF;

	// Packet ID
	tx_dma_buf[frame_idx++] = packet_id;
	checksum += packet_id;

    //Length
    tx_dma_buf[frame_idx++] = (uint8_t)((len >> 8) & 0xFF);	// LEN High
    checksum += (uint8_t)((len >> 8) & 0xFF);
    tx_dma_buf[frame_idx++] = (uint8_t)(len & 0xFF); // LEN Low
    checksum += (uint8_t)(len & 0xFF);

    //CMD
    tx_dma_buf[frame_idx++] = cmd;
    checksum += cmd;

    // Copy Payload
    if (payload_len > 0 && payload != NULL) {
        memcpy(&tx_dma_buf[frame_idx], payload, payload_len);
        for (uint16_t i = 0; i < payload_len; i++) {
            checksum += payload[i];
        }
        frame_idx += payload_len;
    }

    // Checksum
    tx_dma_buf[frame_idx++] = (uint8_t)((checksum >> 8) & 0xFF);
	tx_dma_buf[frame_idx++] = (uint8_t)(checksum & 0xFF);


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

/* Task nhan trach nhiem day cac packet qua uart
 * Lay cac packet tu queue
 * Chi gui khi queue co du lieu
 * Khong co se blocked
 */
void Task_Comm_Handler(void *pvParameters) {
    UART_Packet_t tx_packet;

    App_Comm_Init();

    while(1) {
        // Cho cho den khi storage task nhem data vao
        if (xQueueReceive(qStorageToUart, &tx_packet, portMAX_DELAY) == pdPASS) {
        	// Truyen frame
        	App_Comm_SendFrame(tx_packet.packet_id ,tx_packet.cmd, tx_packet.payload, tx_packet.length);;
        }
    }
}


void App_Comm_Init(void) {
    rx_state = STATE_WAIT_HEADER_H;
    memset(&rx_packet, 0, sizeof(UART_Packet_t));
    old_pos = 0;

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_dma_buf, RX_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT); // Tat Half Tranfer interupt
}

