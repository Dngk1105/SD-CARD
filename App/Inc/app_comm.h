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
#define FRAME_HEADER_H 0x55
#define FRAME_HEADER_L 0xAA
#define MAX_PAYLOAD_SIZE 512

// Cac Packet ID
#define PID_CMD   0x01
#define PID_DATA  0x02
#define PID_ACK   0x07
#define PID_END   0x08

typedef enum {
    STATE_WAIT_HEADER_H = 0,
    STATE_WAIT_HEADER_L,
    STATE_WAIT_PID,
	STATE_WAIT_ADDR_0,
	STATE_WAIT_ADDR_1,
	STATE_WAIT_ADDR_2,
	STATE_WAIT_ADDR_3,
    STATE_WAIT_LEN_H,
    STATE_WAIT_LEN_L,
	STATE_WAIT_CMD,
    STATE_WAIT_PAYLOAD,
    STATE_WAIT_CS_H,
    STATE_WAIT_CS_L
} ParserState_t;


// Danh sach lenh
typedef enum {
	// Lenh tu PC
	CMD_SYS_PING_REQ        = 0x00,
	CMD_GET_SYS_INFO_REQ    = 0x01,
	CMD_DIR_READ_REQ        = 0x02,
	CMD_FILE_READ_REQ       = 0x03,
	CMD_GET_VOL_INFO_REQ    = 0x10,
	CMD_DIR_OPEN_REQ        = 0x11,
	CMD_FILE_DELETE_REQ     = 0x12,
	CMD_DIR_CREATE_REQ      = 0x13,
	CMD_FILE_WRITE_START_REQ= 0x14,
	CMD_FILE_WRITE_DATA_REQ = 0x15,
	CMD_FILE_WRITE_END_REQ  = 0x16,
	CMD_GET_UI_STATUS_REQ   = 0x50,  // PC xin trạng thái truyền

	// Lenh tu STM32
	CMD_SYS_PING_ACK        = 0x80,
	CMD_GET_SYS_INFO_ACK    = 0x81,
	CMD_DATA_CHUNK_ACK      = 0x90,
	CMD_ERROR_ACK           = 0xFF,
	CMD_BENCHMARK_DATA      = 0x96,
	CMD_GET_VOL_INFO_ACK    = 0xA0,
	CMD_DIR_ENTRY_ACK       = 0xA1,
	CMD_DIR_END_ACK         = 0xA2,
	CMD_FILE_READ_START_ACK = 0xA3,
	CMD_FILE_READ_END_ACK   = 0xA4,
	CMD_GENERIC_ACK         = 0xAF,
	CMD_GET_UI_STATUS_ACK   = 0xB0
} CommandID_t;



#pragma pack(push, 1)

typedef struct {
	uint8_t	 packet_id;
    uint8_t  cmd;
    uint16_t length;
    uint8_t  payload[MAX_PAYLOAD_SIZE];
} UART_Packet_t;

typedef struct {
    uint8_t  status;       // 0: OK, >0: FatFs Error
    uint32_t total_kb;
    uint32_t free_kb;
    char     label[12];
} Payload_VolInfo_t;

typedef struct {
    uint32_t fsize;
    uint16_t fdate;
    uint16_t ftime;
    uint8_t  fattrib;
    char     fname[32];
} Payload_FileInfo_t;

typedef struct {
    uint8_t  status;
    uint32_t file_size;
} Payload_FileStart_t;

// Struct trạng thái hệ thống
typedef struct {
    uint8_t  is_mounted;
    uint8_t  transfer_dir;     // 0: Idle, 1: Write(PC->SD), 2: Read(SD->PC)
    uint8_t  transfer_status;
    uint32_t total_bytes;
    uint32_t bytes_processed;
    uint8_t  progress_percent;
    float    speed_kbps;
} UI_Transfer_Live_t;

#pragma pack(pop)


void App_Comm_Init(void);
void App_Comm_ParseByte(uint8_t byte);
void App_Comm_SendFrame(uint8_t packet_id, CommandID_t cmd, uint8_t *payload, uint16_t len);

void Task_Comm_Handler(void *pvParameters);

#endif /* INC_APP_COMM_H_ */
