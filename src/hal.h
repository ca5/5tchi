#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

#define LCD_WIDTH   128
#define LCD_HEIGHT  128

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF

#define BTN_A         (1 << 0)
#define BTN_B         (1 << 1)
#define BTN_C         (1 << 2)
#define BTN_DIAL      (1 << 3)

typedef struct {
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
} RtcTime_t;

void hal_init(void);
uint32_t hal_get_ms(void);
void hal_delay_ms(uint32_t ms);

uint16_t* hal_lcd_get_framebuffer(void);
void hal_lcd_flush(void);
void hal_lcd_set_backlight(uint8_t percent);

uint8_t hal_input_read_buttons(void);
int8_t  hal_input_read_dial_delta(void);
void hal_rtc_get_time(RtcTime_t *out_time);
int  hal_uart_read_line(char *buf, int max_len);
void hal_debug_log(const char *s);
void hal_system_reboot(void);

#endif
