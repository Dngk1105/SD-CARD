/*
 * app_lcd.c
 *
 *  Created on: 30 thg 6, 2026
 *      Author: Admin
 */

#include "app_comm.h"
#include "app_storage.h"
#include "ILI9341_GFX.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

// ==== Font & layout ====
#define FONT_LABEL   FONT1
#define FONT_VALUE   FONT1

#define LCD_WIDTH   320
#define LCD_HEIGHT  240

#define COL_LABEL   10
#define COL_VALUE   120

//	horizontal
#define ROW_FILENAME    20
#define ROW_STATUS      35
#define ROW_PROGRESS    50
#define ROW_SPEED_E2E   65
#define ROW_SPEED_SPI   80
#define ROW_ERR_UART    95
#define ROW_ERR_SPI     110
#define ROW_QUEUE       125
#define ROW_HEAP        140

//	vertical
//#define ROW_FILENAME    20
//#define ROW_STATUS      35
//#define ROW_PROGRESS    50
//#define ROW_SPEED_E2E   70
//#define ROW_SPEED_SPI   85
//#define ROW_ERR_UART    100
//#define ROW_ERR_SPI     115
//#define ROW_QUEUE       130
//#define ROW_HEAP        145

#define COLOR_LABEL     0xFFFF  // trắng
#define COLOR_VALUE     0x07E0  // xanh lá
#define COLOR_ERROR     0xF800  // đỏ
#define COLOR_BG        0x0000  // đen

#define PROGRESS_BAR_W  150
#define PROGRESS_BAR_H  10

static const char* op_type_str(uint8_t t)
{
    switch (t) {
        case 0: return "IDLE";
        case 1: return "UPLOAD";
        case 2: return "DOWNLOAD";
        case 3: return "DELETE";
        case 4: return "MKDIR";
        case 5: return "MOVE";
        default: return "UNKNOWN";
    }
}

static const char* status_str(uint8_t s)
{
    switch (s) {
        case 0: return "Running";
        case 1: return "Paused";
        case 2: return "Error";
        case 3: return "Done";
        default: return "?";
    }
}

// Xóa 1 vùng chữ nhật bằng màu nền, sau đó vẽ text mới đè lên
static void LCD_PrintField(uint16_t x, uint16_t y, uint16_t clear_w, uint16_t clear_h,
                            const char* text, uint16_t color)
{
    ILI9341_DrawFilledRectangleCoord(x, y, x + clear_w, y + clear_h, COLOR_BG);
    ILI9341_DrawText(text, FONT_VALUE, x, y, color, COLOR_BG);
}

// Vẽ thanh progress bar dạng khung + phần lấp đầy
static void LCD_DrawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint16_t fill_w = (uint32_t)w * percent / 100;

    // Khung ngoài
    ILI9341_DrawHollowRectangleCoord(x, y, x + w, y + h, COLOR_LABEL);
    // Xóa phần bên trong trước (đề phòng progress giảm)
    ILI9341_DrawFilledRectangleCoord(x + 1, y + 1, x + w - 1, y + h - 1, COLOR_BG);
    // Phần lấp đầy theo %
    if (fill_w > 2) {
        ILI9341_DrawFilledRectangleCoord(x + 1, y + 1, x + fill_w - 1, y + h - 1, COLOR_VALUE);
    }
}

// Vẽ khung nhãn tĩnh - gọi 1 lần lúc khởi tạo
static void LCD_DrawStaticLayout(void)
{
    ILI9341_DrawText("File:",      FONT_LABEL, COL_LABEL, ROW_FILENAME,  COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("Status:",    FONT_LABEL, COL_LABEL, ROW_STATUS,    COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("Progress:",  FONT_LABEL, COL_LABEL, ROW_PROGRESS,  COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("E2E Speed:", FONT_LABEL, COL_LABEL, ROW_SPEED_E2E, COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("SPI Speed:", FONT_LABEL, COL_LABEL, ROW_SPEED_SPI, COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("UART Err:",  FONT_LABEL, COL_LABEL, ROW_ERR_UART,  COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("SPI Retry:", FONT_LABEL, COL_LABEL, ROW_ERR_SPI,   COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("Queue U/S:", FONT_LABEL, COL_LABEL, ROW_QUEUE,     COLOR_LABEL, COLOR_BG);
    ILI9341_DrawText("Heap:",      FONT_LABEL, COL_LABEL, ROW_HEAP,      COLOR_LABEL, COLOR_BG);
}

// So sánh & chỉ vẽ lại field nào thay đổi (dirty-check)
static void LCD_UpdateDynamicFields(const UI_Transfer_Live_t *cur, const UI_Transfer_Live_t *prev)
{
    char buf[64];

    // --- Filename ---
    if (strncmp(cur->current_filename, prev->current_filename, sizeof(cur->current_filename)) != 0) {
        LCD_PrintField(COL_VALUE, ROW_FILENAME, 200, 12, cur->current_filename, COLOR_VALUE);
    }

    // --- Status + operation type ---
    if ((cur->transfer_status != prev->transfer_status ||
        cur->operation_type  != prev->operation_type) && cur->is_mounted) {
        snprintf(buf, sizeof(buf), "%s (%s)", status_str(cur->transfer_status), op_type_str(cur->operation_type));
        uint16_t color = (cur->transfer_status == 2) ? COLOR_ERROR : COLOR_VALUE;
        LCD_PrintField(COL_VALUE, ROW_STATUS, 200, 12, buf, color);
    }

    // --- Progress bar + % ---
    if (cur->progress_percent != prev->progress_percent) {
        LCD_DrawProgressBar(COL_VALUE, ROW_PROGRESS, PROGRESS_BAR_W, PROGRESS_BAR_H, cur->progress_percent);
        snprintf(buf, sizeof(buf), "%3u%%", cur->progress_percent);
        LCD_PrintField(COL_VALUE + PROGRESS_BAR_W + 10, ROW_PROGRESS, 40, 12, buf, COLOR_VALUE);
    }

    // --- Tốc độ end-to-end ---
    if (cur->end2end_speed_kbps != prev->end2end_speed_kbps) {
    	int end2end_speed_int = (int)cur->end2end_speed_kbps;
    	int end2end_speed_dec = (int)((cur->end2end_speed_kbps - end2end_speed_int) * 10);
    	snprintf(buf, sizeof(buf), "%d.%d KB/s", end2end_speed_int, end2end_speed_dec);
        LCD_PrintField(COL_VALUE, ROW_SPEED_E2E, 150, 12, buf, COLOR_VALUE);
    }

    // --- Tốc độ SPI thuần ---
    if (cur->spi_pure_speed_kbps != prev->spi_pure_speed_kbps) {
    	int spi_pure_speed_int = (int)cur->spi_pure_speed_kbps;
    	int spi_pure_speed_dec = (int)((cur->spi_pure_speed_kbps - spi_pure_speed_int) * 10);
    	snprintf(buf, sizeof(buf), "%d.%d KB/s", spi_pure_speed_int, spi_pure_speed_dec);
        LCD_PrintField(COL_VALUE, ROW_SPEED_SPI, 150, 12, buf, COLOR_VALUE);
    }

    // --- Đếm lỗi UART ---
    if (cur->uart_error_count != prev->uart_error_count) {
        snprintf(buf, sizeof(buf), "%u", cur->uart_error_count);
        uint16_t color = (cur->uart_error_count > 0) ? COLOR_ERROR : COLOR_VALUE;
        LCD_PrintField(COL_VALUE, ROW_ERR_UART, 100, 12, buf, color);
    }

    // --- Đếm retry SPI ---
    if (cur->spi_retry_count != prev->spi_retry_count) {
        snprintf(buf, sizeof(buf), "%u", cur->spi_retry_count);
        uint16_t color = (cur->spi_retry_count > 0) ? COLOR_ERROR : COLOR_VALUE;
        LCD_PrintField(COL_VALUE, ROW_ERR_SPI, 100, 12, buf, color);
    }

    // --- Queue length ---
    if (cur->q_uart_to_storage_len != prev->q_uart_to_storage_len ||
        cur->q_storage_to_uart_len != prev->q_storage_to_uart_len) {
        snprintf(buf, sizeof(buf), "%u / %u", cur->q_uart_to_storage_len, cur->q_storage_to_uart_len);
        LCD_PrintField(COL_VALUE, ROW_QUEUE, 100, 12, buf, COLOR_VALUE);
    }

    // --- Free heap ---
    if (cur->free_heap_bytes != prev->free_heap_bytes) {
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)cur->free_heap_bytes);
        LCD_PrintField(COL_VALUE, ROW_HEAP, 150, 12, buf, COLOR_VALUE);
    }
}

void Task_LCD_Handler(void *pvParameters)
{
    (void)pvParameters;

    UI_Transfer_Live_t cur_status  = {0};
    UI_Transfer_Live_t prev_status;
    memset(&prev_status, 0xFF, sizeof(prev_status)); // ép khác lần đầu để full-draw

    ILI9341_DrawFilledRectangleCoord(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG); // xóa toàn màn hình

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(200); // 5Hz

    vTaskDelay(pdMS_TO_TICKS(2000));
    uint8_t ret = Storage_Get_Live_Status(&cur_status);
    if (ret && cur_status.is_mounted){
		LCD_DrawStaticLayout();
    } else {
        ILI9341_DrawText("SD NOT FOUND", FONT_LABEL, COL_LABEL, ROW_FILENAME,  COLOR_ERROR, COLOR_BG);
    }

    for (;;) {
        uint8_t ret = Storage_Get_Live_Status(&cur_status);

        if (ret == 1) {
            if (cur_status.is_mounted) LCD_UpdateDynamicFields(&cur_status, &prev_status);
            prev_status = cur_status;
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
