/*
 * app_storage.c
 *
 *  Created on: 18 thg 6, 2026
 *      Author: PC
 */
#include "app_storage.h"


uint8_t work_buffer[BENCH_CHUNK_SIZE];
void Task_Storage_Handler(void *pvParameters) {
    FATFS fs;
    FIL fil;
    UINT bw;

    // Tắt cả 2 đèn trước khi bắt đầu
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);

    /* --- BƯỚC 1: MOUNT THẺ SD --- */
    if (f_mount(&fs, "", 1) != FR_OK) {
        // Lỗi: Bật LED Đỏ
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }

    /* --- BƯỚC 2: TIẾN HÀNH BÀI TEST GHI --- */
    if (f_open(&fil, "bench.dat", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {

        memset(work_buffer, 0xAA, BENCH_CHUNK_SIZE);

        // Vòng lặp bắn 1MB data
        for (uint32_t i = 0; i < (BENCH_FILE_SIZE / BENCH_CHUNK_SIZE); i++) {
            if (f_write(&fil, work_buffer, BENCH_CHUNK_SIZE, &bw) != FR_OK || bw != BENCH_CHUNK_SIZE) {
                // Lỗi ghi: Bật Đỏ, tắt Xanh, đóng file
                HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_RESET);
                f_close(&fil);
                vTaskDelete(NULL);
            }

            // Đang ghi: Nháy đèn Xanh liên tục
            HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_13);
        }

        f_close(&fil);

        // THÀNH CÔNG: Tắt đèn Đỏ, Bật đèn Xanh sáng cố định
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);

    } else {
        // Lỗi không tạo được file: Bật LED Đỏ
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
    }

    /* Xong việc -> Hủy Task */
    vTaskDelete(NULL);
}
