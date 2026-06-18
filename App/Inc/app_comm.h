/*
 * app_comm.h
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */

#ifndef INC_APP_COMM_H_
#define INC_APP_COMM_H_

#include <stdint.h>


// Khung truyen du lieu
#define FRAME_SYNC1 0xAA
#define FRAME_SYNC2 0x55
#define FRAME_TAIL  0x0D
#define MAX_PAYLOAD_SIZE 512

typedef enum {
    STATE_WAIT_SYNC1 = 0,
    STATE_WAIT_SYNC2,
    STATE_WAIT_CMD,
    STATE_WAIT_LEN_L,
    STATE_WAIT_LEN_H,
    STATE_WAIT_PAYLOAD,
    STATE_WAIT_CHECKSUM,
    STATE_WAIT_TAIL
} ParserState_t;


// Danh sach lenh
typedef enum {
	// Lenh tu PC
	CMD_SYS_PING_REQ        = 0x00,
	CMD_GET_SYS_INFO_REQ    = 0x01,
	CMD_DIR_READ_REQ        = 0x02,
	CMD_FILE_READ_REQ       = 0x03,
	CMD_BENCHMARK_REQ		= 0x06,

	// Lenh tu STM32
	CMD_SYS_PING_ACK        = 0x80,
	CMD_GET_SYS_INFO_ACK    = 0x81,
	CMD_DATA_CHUNK_ACK      = 0x90,
	CMD_ERROR_ACK           = 0xFF,
	CMD_BENCHMARK_DATA      = 0x96
} CommandID_t;


typedef struct {
    uint8_t  cmd;
    uint16_t length;
    uint8_t  payload[MAX_PAYLOAD_SIZE];
} UART_Packet_t;

void App_Comm_Init(void);
void App_Comm_ParseByte(uint8_t byte);
void App_Comm_SendFrame(CommandID_t cmd, uint8_t *payload, uint16_t len);

void Task_Comm_Handler(void *pvParameters);

#endif /* INC_APP_COMM_H_ */
