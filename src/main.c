#include "hal.h"
#include "stage.h"

int main(void) {
    hal_init();
    hal_lcd_set_backlight(60);

    uint16_t *fb = hal_lcd_get_framebuffer();
    RtcTime_t rtc;
    hal_rtc_get_time(&rtc);

    StageState_t stage;
    stage_init(&stage, &rtc, hal_get_ms());

    uint8_t  prev_btns          = 0;
    uint32_t confirm_press_ms   = 0;
    bool     confirm_long_fired = false;
    uint32_t c_press_ms         = 0;
    bool     c_long_fired       = false;
    uint32_t last_rotate_ms     = 0;

    while (1) {
        uint32_t now_ms = hal_get_ms();
        hal_rtc_get_time(&rtc);

        char uart_line[128];
        while (hal_uart_read_line(uart_line, sizeof(uart_line)) > 0) {
            stage_process_uart_line(&stage, uart_line, &rtc, now_ms);
        }

        uint8_t btns = hal_input_read_buttons();
        int8_t  dial = hal_input_read_dial_delta();

        if (dial != 0) {
            last_rotate_ms = now_ms;
            stage_on_dial_rotate(&stage, dial);
        }

        /* B and Dial Press act identically as Confirm (short press) / Menu toggle (2s hold) */
        bool confirm_down = (btns & (BTN_B | BTN_DIAL)) != 0;
        bool confirm_prev = (prev_btns & (BTN_B | BTN_DIAL)) != 0;

        if (confirm_down && !confirm_prev) {
            confirm_press_ms   = now_ms;
            confirm_long_fired = false;
        } else if (confirm_down && !confirm_long_fired) {
            if ((now_ms - confirm_press_ms) >= 2000) {
                confirm_long_fired = true;
                stage_toggle_menu(&stage);
            }
        } else if (!confirm_down && confirm_prev) {
            uint32_t held = now_ms - confirm_press_ms;
            bool dial_used = (prev_btns & BTN_DIAL) != 0;
            bool mask_ok = !dial_used || (now_ms - last_rotate_ms) > 150;
            if (!confirm_long_fired && held >= 50 && held <= 800 && mask_ok) {
                stage_on_dial_short_press(&stage, &rtc, now_ms);
            }
        }

        /* A button: step forward (+1) */
        bool a_down = (btns & BTN_A) != 0;
        bool a_prev = (prev_btns & BTN_A) != 0;
        if (a_down && !a_prev) {
            stage_on_dial_rotate(&stage, 1);
        }

        /* C button: Beat reset on live screen; step backward (-1) / cancel on menu & setup */
        bool c_down = (btns & BTN_C) != 0;
        bool c_prev = (prev_btns & BTN_C) != 0;
        bool in_ui  = stage.menu_open || stage.setup_open;
        if (c_down && !c_prev) {
            if (!in_ui) {
                stage_on_beat_reset(&stage, now_ms);
                c_long_fired = true;
            } else {
                c_press_ms   = now_ms;
                c_long_fired = false;
            }
        } else if (c_down && !c_long_fired && in_ui) {
            if ((now_ms - c_press_ms) >= 600) {
                c_long_fired = true;
                stage_on_cancel(&stage);
            }
        } else if (!c_down && c_prev && in_ui) {
            uint32_t held = now_ms - c_press_ms;
            if (!c_long_fired && held >= 30) {
                stage_on_dial_rotate(&stage, -1);
            }
        }

        prev_btns = btns;

        stage_render(&stage, fb, &rtc, now_ms);

        hal_lcd_flush();
        hal_delay_ms(16);
    }

    return 0;
}
