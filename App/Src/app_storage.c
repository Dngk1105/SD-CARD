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

// cap nhat g_live_status an toan qua mutex
// chi ghi nhung truong can thiet, khong ghi toan bo struct
static void LiveStatus_Update(UI_Transfer_Live_t *patch){
	if (xSemaphoreTake(xMutex_UI_Live, pdMS_TO_TICKS(100)) == pdTRUE){
		memcpy(&g_live_status, patch, sizeof(UI_Transfer_Live_t));
		xSemaphoreGive(xMutex_UI_Live);
	}
}

// Getter
void Storage_Get_Live_Status(UI_Transfer_Live_t *out_status) {
    if (out_status == NULL) return;

    // Lay mutex
    if (xSemaphoreTake(xMutex_UI_Live, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(out_status, &g_live_status, sizeof(UI_Transfer_Live_t));
        xSemaphoreGive(xMutex_UI_Live);
    }
}


/* Task duy nhat giao tiep voi the nho
 * Lay cac goi tin tu queue
 * Truy cap bo nho
 * Xuat ra queue phu hop
 */
void Task_Storage_Handler(void *pvParameters) {
    FATFS fs;
    static UART_Packet_t rx_packet;
	static UART_Packet_t tx_packet;
	FRESULT fr;
	FIL current_file;
	static uint8_t is_downloading = 0;
	static TickType_t transfer_start_time = 0;

	// Khoi tao mutex
	xMutex_UI_Live = xSemaphoreCreateMutex();

    // Mount sd
	vTaskDelay(pdMS_TO_TICKS(1000));
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
    						vol_info.total_kb = total_sect >> 1;
							vol_info.free_kb =  fre_sect >> 1;

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
						// Mo thu muc theo duong dan, dung vong lap f_readdir tra ve CMD_DIR_ENTRY_ACK
    					DIR dir;
    					FILINFO file_info;
    					Payload_FileInfo_t item_info;

    					// Mo thu muc
    					char *path = (char*) rx_packet.payload;
    					fr = f_opendir(&dir, path);
    					if (fr == FR_OK){
    						while (1){
    							fr = f_readdir(&dir, &file_info); // doc entries theo thu tu
    							// record cuoi co ky tu dau la 0;
    							if (fr != FR_OK || file_info.fname[0] == 0) break;

    							memset(&item_info, 0, sizeof(Payload_FileInfo_t));
    							item_info.fsize = file_info.fsize;
								item_info.fdate = file_info.fdate;
								item_info.ftime = file_info.ftime;
								item_info.fattrib = file_info.fattrib;
								strncpy(item_info.fname, file_info.fname, sizeof(item_info.fname) - 1);
								strncpy(item_info.altname, file_info.altname, sizeof(item_info.altname) - 1);

								tx_packet.cmd = CMD_DIR_ENTRY_ACK;
								tx_packet.length = sizeof(Payload_FileInfo_t);
								memcpy(tx_packet.payload, &item_info, tx_packet.length);

								//portMAX_DELAY: Cho entries truoc gui xong
								xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
    						}
    						f_closedir(&dir);
    					}

    					tx_packet.cmd = CMD_DIR_END_ACK;
						tx_packet.length = 1;
						tx_packet.payload[0] = (uint8_t)fr; // kem ma loi

						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
						break;

    				case CMD_FILE_DELETE_REQ:
    				{
						// Xoa file hoac thu muc rong bang f_unlink
						char *path = (char*) rx_packet.payload;

						// Cap nhat live status
						{
							UI_Transfer_Live_t patch = g_live_status;
							patch.operation_type  = OP_DELETE;
							patch.transfer_status = 0; // Running
							patch.last_error_code = 0;
							strncpy(patch.current_filename, path, sizeof(patch.current_filename) - 1);
							patch.total_bytes     = 0;
							patch.bytes_processed = 0;
							patch.progress_percent= 0;
							patch.speed_kbps      = 0.0f;
							LiveStatus_Update(&patch);
						}

						fr = f_unlink(path);

						// Cap nhat live status: Ket qua xoa
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.transfer_status = (fr == FR_OK) ? 3 : 2; // Done / Error
                            patch.last_error_code = (uint8_t) fr;
                            patch.operation_type  = OP_IDLE;
                            LiveStatus_Update(&patch);
                        }

						// Bắn ACK báo cáo kết quả xóa lên PC
						tx_packet.cmd = CMD_GENERIC_ACK;
						tx_packet.length = 1;
						tx_packet.payload[0] = (uint8_t)fr;
						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
						break;
    				}
					case CMD_DIR_CREATE_REQ:
					{
						// Tao thu muc moi bang f_mkdir
						char *path = (char*) rx_packet.payload;

                        // Cap nhat live status: bat dau tao thu muc
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.operation_type  = OP_MKDIR;
                            patch.transfer_status = 0; // Running
                            patch.last_error_code = 0;
                            strncpy(patch.current_filename, path, sizeof(patch.current_filename) - 1);
                            LiveStatus_Update(&patch);
                        }

						fr = f_mkdir(path);

                        // Cap nhat live status: ket qua tao thu muc
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.transfer_status = (fr == FR_OK) ? 3 : 2; // Done / Error
                            patch.last_error_code = (uint8_t) fr;
                            patch.operation_type  = OP_IDLE;
                            LiveStatus_Update(&patch);
                        }

						// ACK
						tx_packet.cmd = CMD_GENERIC_ACK;
						tx_packet.length = 1;
						tx_packet.payload[0] = (uint8_t)fr;
						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
						break;
					}

					// DOWNLOAD & UPLOAD
					// DOWNLOAD (SD -> PC)
					case CMD_FILE_READ_REQ:
					{
						// Mo file (f_open read)
						char *path = (char*) rx_packet.payload;
						transfer_start_time = xTaskGetTickCount();
						fr = f_open(&current_file, path, FA_READ);

                        // Cap nhat live status: bat dau download
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.operation_type   = OP_DOWNLOAD;
                            patch.transfer_status  = (fr == FR_OK) ? 0 : 2; // Running / Error
                            patch.last_error_code  = (uint8_t) fr;
                            patch.total_bytes      = (fr == FR_OK) ? f_size(&current_file) : 0;
                            patch.bytes_processed  = 0;
                            patch.progress_percent = 0;
                            patch.speed_kbps       = 0.0f;
                            strncpy(patch.current_filename, path, sizeof(patch.current_filename) - 1);
                            LiveStatus_Update(&patch);
                        }

						// Dong goi start ACK kem size file
						Payload_FileStart_t start_info;
						start_info.status = (uint8_t)fr;
						start_info.file_size = (fr == FR_OK) ? f_size(&current_file) : 0;

						tx_packet.cmd = CMD_FILE_READ_START_ACK;
						tx_packet.length = sizeof(Payload_FileStart_t);
						memcpy(tx_packet.payload, &start_info, tx_packet.length);
						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);

						// Loop f_read ban CMD_DATA_CHUNK_ACK
						if (fr == FR_OK) {
							is_downloading = 1;
						}
						break;
					}

					case CMD_FILE_READ_NEXT_REQ:
					{
						if (is_downloading) {
							UINT bytes_read;
							fr = f_read(&current_file, tx_packet.payload, MAX_PAYLOAD_SIZE, &bytes_read);

							// Doc thanh cong va van con data
							if (fr == FR_OK && bytes_read > 0) {
								tx_packet.cmd = CMD_DATA_CHUNK_ACK;
								tx_packet.length = bytes_read;
								xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);

                                // Cap nhat tien do download
                                {
                                    UI_Transfer_Live_t patch = g_live_status;
                                    patch.bytes_processed += bytes_read;

                                    if (patch.total_bytes > 0) {
                                        patch.progress_percent = (uint8_t)(
                                            (patch.bytes_processed * 100UL) / patch.total_bytes
                                        );
                                    }

                                    // Tinh toc do: KB/s
                                    uint32_t elapsed_ms = (xTaskGetTickCount() - transfer_start_time) * portTICK_PERIOD_MS;
                                    if (elapsed_ms > 0) {
                                        patch.speed_kbps = ((float) patch.bytes_processed / 1024.0f)
                                                           / (elapsed_ms / 1000.0f);
                                    }
                                    LiveStatus_Update(&patch);
                                }
							}
							// Cuoi file hoac co loi
							else {
								f_close(&current_file);
								is_downloading = 0;

                                // Cap nhat live status: ket thuc download
                                {
                                    UI_Transfer_Live_t patch = g_live_status;
                                    patch.transfer_status  = (fr == FR_OK) ? 3 : 2; // Done / Error
                                    patch.last_error_code  = (uint8_t) fr;
                                    patch.progress_percent = (fr == FR_OK) ? 100 : patch.progress_percent;
                                    patch.operation_type   = OP_IDLE;
                                    patch.speed_kbps       = 0.0f;
                                    LiveStatus_Update(&patch);
                                }

								// END ACK
								tx_packet.cmd = CMD_FILE_READ_END_ACK;
								tx_packet.length = 1;
								tx_packet.payload[0] = (uint8_t)fr;
								xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
							}
						}
						break;
					}

					// UPLOAD (PC -> SD)
					case CMD_FILE_WRITE_START_REQ:
					{
						// Mo file (f_open write create always)
						char *path = (char*) rx_packet.payload;
						transfer_start_time = xTaskGetTickCount();
						fr = f_open(&current_file, path, FA_CREATE_ALWAYS | FA_WRITE);

                        // Cap nhat live status: bat dau upload
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.operation_type   = OP_UPLOAD;
                            patch.transfer_status  = (fr == FR_OK) ? 0 : 2; // Running / Error
                            patch.last_error_code  = (uint8_t) fr;
                            patch.bytes_processed  = 0;
                            patch.total_bytes      = 0; // Chua biet truoc, PC se gui tung chunk
                            patch.progress_percent = 0;
                            patch.speed_kbps       = 0.0f;
                            strncpy(patch.current_filename, path, sizeof(patch.current_filename) - 1);
                            LiveStatus_Update(&patch);
                        }

						// Tra CMD_GENERIC_ACK(0)
						tx_packet.cmd = CMD_GENERIC_ACK;
						tx_packet.length = 1;
						tx_packet.payload[0] = (uint8_t)fr;
						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
						break;
					}

					case CMD_FILE_WRITE_DATA_REQ:
					{
						// Ghi payload vao file (f_write)
						UINT bytes_written;
						fr = f_write(&current_file, rx_packet.payload, rx_packet.length, &bytes_written);
						uint8_t write_ok = (fr == FR_OK && bytes_written == rx_packet.length);

                        // Cap nhat tien do upload
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.bytes_processed += bytes_written;
                            patch.last_error_code  = (uint8_t) fr;
                            patch.transfer_status  = write_ok ? 0 : 2; // Running / Error

                            // Tinh toc do: KB/s
                            uint32_t elapsed_ms = (xTaskGetTickCount() - transfer_start_time) * portTICK_PERIOD_MS;
                            if (elapsed_ms > 0) {
                                patch.speed_kbps = ((float)patch.bytes_processed / 1024.0f)
                                                   / (elapsed_ms / 1000.0f);
                            }
                            LiveStatus_Update(&patch);
                        }

						// Tra CMD_GENERIC_ACK(0) de bao PC gui tiep
						tx_packet.cmd = CMD_GENERIC_ACK;
						tx_packet.length = 1;
						// Check luon xem so byte ghi thuc te co khop voi payload length khong
						tx_packet.payload[0] = write_ok ? 0x00 : 0xFF;
						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
						break;
					}

					case CMD_FILE_WRITE_END_REQ:
					{
						// Dong file (f_close)
						fr = f_close(&current_file);

                        // Cap nhat live status: ket thuc upload
                        {
                            UI_Transfer_Live_t patch = g_live_status;
                            patch.transfer_status  = (fr == FR_OK) ? 3 : 2; // Done / Error
                            patch.last_error_code  = (uint8_t) fr;
                            patch.progress_percent = (fr == FR_OK) ? 100 : patch.progress_percent;
                            patch.operation_type   = OP_IDLE;
                            patch.speed_kbps       = 0.0f;
                            LiveStatus_Update(&patch);
                        }

						// Tra CMD_GENERIC_ACK
						tx_packet.cmd = CMD_GENERIC_ACK;
						tx_packet.length = 1;
						tx_packet.payload[0] = (uint8_t)fr;
						xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
						break;
					}

					// UI STATUS API
					case CMD_GET_UI_STATUS_REQ:
						// [TODO]: Lay Mutex, copy g_live_status vao payload va gui CMD_GET_UI_STATUS_ACK
                        UI_Transfer_Live_t status_snapshot;

                        if (xSemaphoreTake(xMutex_UI_Live, pdMS_TO_TICKS(100)) == pdTRUE) {
                            memcpy(&status_snapshot, &g_live_status, sizeof(UI_Transfer_Live_t));
                            xSemaphoreGive(xMutex_UI_Live);

                            tx_packet.cmd    = CMD_GET_UI_STATUS_ACK;
                            tx_packet.length = sizeof(UI_Transfer_Live_t);
                            memcpy(tx_packet.payload, &status_snapshot, tx_packet.length);
                        } else {
                            // Timeout lay mutex -> bao loi
                            tx_packet.cmd        = CMD_ERROR_ACK;
                            tx_packet.length     = 1;
                            tx_packet.payload[0] = 0xFE; // Ma loi: mutex timeout
                        }

                        xQueueSend(qStorageToUart, &tx_packet, portMAX_DELAY);
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
				// Nen co the bo qua hoac dung cho viec truyen streaming toc do cao khong can ACK.
    		}


    	}
    }
}
