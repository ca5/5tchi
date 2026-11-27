#include "hal.h"
#include "stage.h"

#define CLOCK_SLEEP_TIMEOUT_MS 10000u

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
    uint32_t last_activity_ms   = hal_get_ms();
    bool     sleeping           = false;
    uint8_t  last_clock_sec     = 255;

    while (1) {
        uint32_t now_ms = hal_get_ms();
        hal_rtc_get_time(&rtc);

        char uart_line[128];
        while (hal_uart_read_line(uart_line, sizeof(uart_line)) > 0) {
            stage_process_uart_line(&stage, uart_line, &rtc, now_ms);
            last_clock_sec = 255;
        }

        uint8_t btns = hal_input_read_buttons();
        int8_t  dial = hal_input_read_dial_delta();

        bool any_input = (btns != 0) || (dial != 0);
        if (any_input) {
            last_activity_ms = now_ms;
            if (sleeping) {
                sleeping = false;
                hal_lcd_sleep(false);
                last_clock_sec = 255;
                /* Consume the wake press so it doesn't accidentally trigger an action */
                prev_btns = btns;
                continue;
            }
        }

        if (stage.app_mode == APP_MODE_CLOCK && !stage.menu_open && !stage.setup_open) {
            /* In clock mode, only allow B/Dial 2s hold to open menu, and auto-sleep after 10s */
            bool confirm_down = (btns & (BTN_B | BTN_DIAL)) != 0;
            bool confirm_prev = (prev_btns & (BTN_B | BTN_DIAL)) != 0;
            if (confirm_down && !confirm_prev) {
                confirm_press_ms   = now_ms;
                confirm_long_fired = false;
            } else if (confirm_down && !confirm_long_fired) {
                if ((now_ms - confirm_press_ms) >= 2000u) {
                    confirm_long_fired = true;
                    stage_toggle_menu(&stage);
                }
            }
            prev_btns = btns;

            if (!stage.menu_open && !sleeping && (now_ms - last_activity_ms) >= CLOCK_SLEEP_TIMEOUT_MS) {
                sleeping = true;
                hal_lcd_sleep(true);
            }

            if (sleeping) {
                hal_delay_ms(50);
                continue;
            }

            if (rtc.sec != last_clock_sec || stage.menu_open) {
                last_clock_sec = rtc.sec;
                stage_render(&stage, fb, &rtc, now_ms);
                hal_lcd_flush();
            }
            hal_delay_ms(30);
            continue;
        }

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
            if ((now_ms - confirm_press_ms) >= 2000u) {
                confirm_long_fired = true;
                stage_toggle_menu(&stage);
            }
        } else if (!confirm_down && confirm_prev) {
            uint32_t held = now_ms - confirm_press_ms;
            bool dial_used = (prev_btns & BTN_DIAL) != 0;
            bool mask_ok = !dial_used || (now_ms - last_rotate_ms) > 150u;
            if (!confirm_long_fired && held >= 50u && held <= 800u && mask_ok) {
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
            if ((now_ms - c_press_ms) >= 600u) {
                c_long_fired = true;
                stage_on_cancel(&stage);
            }
        } else if (!c_down && c_prev && in_ui) {
            uint32_t held = now_ms - c_press_ms;
            if (!c_long_fired && held >= 30u) {
                stage_on_dial_rotate(&stage, -1);
            }
        }

        prev_btns = btns;
        last_clock_sec = 255;

        stage_render(&stage, fb, &rtc, now_ms);

        hal_lcd_flush();
        hal_delay_ms(16);
    }

    return 0;
}
