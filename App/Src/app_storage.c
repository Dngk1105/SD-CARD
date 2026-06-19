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

// Semaphore cho bien trang thai (UI)
UI_Transfer_Live_t g_live_status = {0};
SemaphoreHandle_t xMutex_UI_Live = NULL;

/* Task duy nhat giao tiep voi the nho
 * Lay cac goi tin tu queue
 * Truy cap bo nho
 * Xuat ra queue phu hop
 */
void Task_Storage_Handler(void *pvParameters) {
    FATFS fs;
    UART_Packet_t rx_packet;
	UART_Packet_t tx_packet;
	FRESULT fr;
	FIL current_file;

	// Khoi tao mutex
	xMutex_UI_Live = xSemaphoreCreateMutex();

    // Mount sd
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        // Loi: Bat LED G14
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
        g_live_status.is_mounted = 0;
    } else {
    	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);
        g_live_status.is_mounted = 1;
    }

    while (1){
    	// Cho du lieu trong queue gui tu uart
    	if (xQueueReceive(qUartToStorage, &rx_packet, portMAX_DELAY) == pdPASS){
    		// reset packet
    		memset(&tx_packet, 0, sizeof(UART_Packet_t));

    		if (rx_packet.packet_id == PID_CMD){
    			tx_packet.packet_id = PID_ACK; // cmd cần ack

    			switch (rx_packet.cmd) {
    				case CMD_SYS_PING_REQ:
    					// TEST 1: PC ping STM32 -> STM32 Pong
    					tx_packet.cmd = CMD_SYS_PING_ACK;
    					tx_packet.length = 4;
    					memcpy(tx_packet.payload, "PONG", 4);

    					xQueueSend(qStorageToUart, &tx_packet, 0);
    					break;

    				case CMD_GET_VOL_INFO_REQ:
    					// Cung cap thong tin cua the SD
    					DWORD fre_clust, fre_sect, total_sect;
    					FATFS *pfs;
    					Payload_VolInfo_t vol_info = {0};

    					fr = f_getfree("", &fre_clust, &pfs);
    					vol_info.status = fr;

    					if (fr == FR_OK){
    						vol_info.sector_size = 512;
    						// Tong sector va sector trong
    						// Doi sang KB
    						total_sect = (pfs->n_fatent -2) * pfs->csize;
    						fre_sect = fre_clust * pfs->csize;

    						// Vi mot sector = 512 nen /2 la xong
    						// [TODO]: ranh thi fix nhe
    						vol_info.total_kb = total_sect / 2;
							vol_info.free_kb =  fre_sect / 2;

							// Thong so FAT
							f_getlabel("", vol_info.label, 0);
							vol_info.fs_type = pfs->fs_type;
							vol_info.cluster_size = pfs->csize;
							vol_info.free_clusters = fre_clust;
							vol_info.last_alloc_cluster = pfs->last_clst; //Truong hop phan manh
    					}

    					vol_info.uptime_ms = xTaskGetTickCount();

    					// Dong goi
						tx_packet.cmd = CMD_GET_VOL_INFO_ACK;
						tx_packet.length = sizeof(Payload_VolInfo_t);
						memcpy(tx_packet.payload, &vol_info, tx_packet.length);

						xQueueSend(qStorageToUart, &tx_packet, 0);
    					break;

    				case CMD_DIR_OPEN_REQ:
						// [TODO]: Mo thu muc theo duong dan, dung vong lap f_readdir tra ve CMD_DIR_ENTRY_ACK
						break;

    				case CMD_FILE_DELETE_REQ:
						// [TODO]: Xoa file hoac thu muc rong bang f_unlink
						break;

    				case CMD_DIR_CREATE_REQ:
						// [TODO]: Tao thu muc moi bang f_mkdir
						break;


					// DOWNLOAD & UPLOAD
					// DOWNLOAD (SD -> PC)
					case CMD_FILE_READ_REQ:
						// [TODO]: Mo file (f_open read), lay size, ban CMD_FILE_READ_START_ACK, sau do loop f_read ban CMD_DATA_CHUNK_ACK
						break;

					// UPLOAD (PC -> SD)
					case CMD_FILE_WRITE_START_REQ:
						// [TODO]: Mo file (f_open write create always), tra CMD_GENERIC_ACK(0)
						break;

					case CMD_FILE_WRITE_DATA_REQ:
						// [TODO]: Ghi payload vao file (f_write), tra CMD_GENERIC_ACK(0) de bao PC gui tiep
						break;

					case CMD_FILE_WRITE_END_REQ:
						// [TODO]: Dong file (f_close), tra CMD_GENERIC_ACK
						break;

					// UI STATUS API
					case CMD_GET_UI_STATUS_REQ:
						// [TODO]: Lay Mutex, copy g_live_status vao payload va gui CMD_GET_UI_STATUS_ACK
						break;

					// CÁC LỆNH TEST KHÁC
					case CMD_GET_SYS_INFO_REQ:
						tx_packet.cmd = CMD_GET_SYS_INFO_ACK;
						char info_msg[] = "SD OK, Ready for API";
						tx_packet.length = strlen(info_msg);
						memcpy(tx_packet.payload, info_msg, tx_packet.length);
						xQueueSend(qStorageToUart, &tx_packet, 0);
						break;

					default:
						// Lenh khong ton tai hoac khong ho tro
						tx_packet.cmd = CMD_ERROR_ACK;
						tx_packet.length = 1;
						tx_packet.payload[0] = 0xEE; // Ma loi tuy chon (VD: Unknown Command)
						xQueueSend(qStorageToUart, &tx_packet, 0);
						break;
    			}
    		}
    		// Xu li khi nhan data tu PC
    		else if (rx_packet.packet_id == PID_DATA){
    			// [TODO]: Kien truc Handshaking hien tai dung chung PID_CMD cho CMD_FILE_WRITE_DATA_REQ
				// Nen nhom nay co the bo qua hoac dung cho viec truyen streaming toc do cao khong can ACK.
    		}


    	}
    }
}
