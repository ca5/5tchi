#include "hal.h"
#include "stage.h"

int main(void) {
    hal_init();
    hal_lcd_set_backlight(60);
    hal_debug_log("[DBG] 7:bl_ok\n");

    uint16_t *fb = hal_lcd_get_framebuffer();
    RtcTime_t rtc;
    hal_rtc_get_time(&rtc);

    StageState_t stage;
    stage_init(&stage, &rtc, hal_get_ms());
    hal_debug_log("[DBG] 8:stage_init_ok\n");

    uint8_t  prev_btns        = 0;
    uint32_t dial_press_start = 0;
    bool     dial_long_fired  = false;
    uint32_t last_rotate_ms   = 0;
    bool     first_frame      = true;

    while (1) {
        uint32_t now_ms = hal_get_ms();
        hal_rtc_get_time(&rtc);

        char uart_line[128];
        while (hal_uart_read_line(uart_line, sizeof(uart_line)) > 0) {
            stage_process_uart_line(&stage, uart_line, &rtc, now_ms);
        }

        uint8_t  btns   = hal_input_read_buttons();
        int8_t   dial   = hal_input_read_dial_delta();

        if (dial != 0) {
            last_rotate_ms = now_ms;
            stage_on_dial_rotate(&stage, dial);
        }

        bool dial_down = (btns & BTN_DIAL) != 0;
        bool dial_prev = (prev_btns & BTN_DIAL) != 0;

        if (dial_down && !dial_prev) {
            dial_press_start = now_ms;
            dial_long_fired  = false;
        } else if (dial_down && !dial_long_fired) {
            if ((now_ms - dial_press_start) >= 2000) {
                dial_long_fired = true;
                stage_toggle_menu(&stage);
            }
        } else if (!dial_down && dial_prev) {
            uint32_t held = now_ms - dial_press_start;
            if (!dial_long_fired && held >= 50 && held <= 800 && (now_ms - last_rotate_ms) > 150) {
                stage_on_dial_short_press(&stage, &rtc, now_ms);
            }
        }

        /* A and B buttons are intentionally guarded (ignored) during live performance */
        bool c_down = (btns & BTN_C) != 0;
        bool c_prev = (prev_btns & BTN_C) != 0;
        if (c_down && !c_prev) {
            stage_on_beat_reset(&stage, now_ms);
        }

        prev_btns = btns;

        stage_render(&stage, fb, &rtc, now_ms);
        if (first_frame) hal_debug_log("[DBG] 9:render_ok\n");

        hal_lcd_flush();
        if (first_frame) hal_debug_log("[DBG] 10:flush_ok\n");

        hal_delay_ms(16);
        if (first_frame) {
            hal_debug_log("[DBG] 11:delay_ok\n");
            first_frame = false;
        }
    }

    return 0;
}
