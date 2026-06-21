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
#define MAX_PAYLOAD_SIZE 1024

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
	CMD_FILE_READ_NEXT_REQ  = 0x04,
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
    uint8_t  status;            // Trang thai tra ve (FR_OK neu thanh cong)

    uint32_t total_kb;          // Tong dung luong the SD (KB)
    uint32_t free_kb;           // Dung luong con trong (KB)

    char     label[12];         // Ten volume (VD: "DATA", "SDCARD")

    uint8_t  fs_type;           // Loai FAT (12/16/32/exFAT)
    uint16_t sector_size;       // Kich thuoc sector (byte)
    uint16_t cluster_size;      // So sector moi cluster

    uint32_t free_clusters;     // So cluster trong hien tai
    uint32_t last_alloc_cluster;// Cluster cap phat gan nhat

    uint32_t uptime_ms;         // Thoi gian he thong da chay (ms)
} Payload_VolInfo_t;

typedef struct {
    uint32_t fsize;      // Kích thước file (byte)
    uint16_t fdate;      // Ngày tạo/sửa (định dạng FAT)
    uint16_t ftime;      // Giờ tạo/sửa (định dạng FAT)
    uint8_t  fattrib;    // Thuộc tính file (read-only, hidden, dir,...)
    char     fname[128]; // Tên file đầy đủ (long file name)
    char     altname[14];// Tên file ngắn 8.3 (alternative name)
} Payload_FileInfo_t;

typedef struct {
    uint8_t  status;
    uint32_t file_size;
} Payload_FileStart_t;

// Struct trạng thái hệ thống
typedef enum {
	OP_IDLE = 0,
	OP_UPLOAD,
	OP_DOWNLOAD,
	OP_DELETE,
	OP_MKDIR,
	OP_MOVE
} UI_OpType_t;

typedef struct {
    uint8_t  is_mounted;
    uint8_t  operation_type;   // UI_OpType_t
    uint8_t  transfer_status;  // 0: Running, 1: Paused, 2: Error, 3: Done
    uint8_t  last_error_code;  // Ma loi FatFs (FRESULT)

    char     current_filename[64]; // Ten file dang xu ly

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
