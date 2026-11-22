#ifndef STAGE_H
#define STAGE_H

#include "hal.h"
#include <stdbool.h>
#include <stdint.h>

#define STAGE_MAX_PARTS 8
#define STAGE_LABEL_LEN 12

typedef enum {
    VIEW_STANDARD = 0,
    VIEW_BIG_CLOCK,
    VIEW_LAP_HISTORY,
    VIEW_COUNT
} StageView_t;

typedef enum {
    MENU_PREV_PART = 0,
    MENU_DEADLINE_PLUS,
    MENU_DEADLINE_MINUS,
    MENU_SETUP_SETLIST,
    MENU_RESET_TIMER,
    MENU_CLOSE,
    MENU_ITEM_COUNT
} StageMenuItem_t;

typedef struct {
    char     label[STAGE_LABEL_LEN];
    uint16_t planned_sec;
    uint16_t bpm;
} StagePart_t;

typedef struct {
    StagePart_t parts[STAGE_MAX_PARTS];
    uint8_t     part_count;
    uint8_t     current_part;
    bool        running;
    bool        finished;

    int32_t     deadline_sec_of_day;
    int32_t     rtc_offset_sec;
    uint32_t    part_start_ms;
    uint32_t    beat_origin_ms;
    int32_t     actual_sec[STAGE_MAX_PARTS];
    int32_t     cumulative_diff_sec;

    StageView_t     view;
    bool            menu_open;
    StageMenuItem_t menu_cursor;

    bool        setup_open;
    uint8_t     setup_cursor;
    bool        setup_editing;
    bool        setup_part_open;
    uint8_t     setup_part_field;
    uint8_t     setup_name_pos;
    bool        uart_has_explicit_end;
} StageState_t;

void stage_init(StageState_t *st, const RtcTime_t *now_rtc, uint32_t now_ms);
void stage_on_dial_short_press(StageState_t *st, const RtcTime_t *now_rtc, uint32_t now_ms);
void stage_on_dial_rotate(StageState_t *st, int8_t delta);
void stage_on_beat_reset(StageState_t *st, uint32_t now_ms);
void stage_toggle_menu(StageState_t *st);
void stage_process_uart_line(StageState_t *st, const char *line, const RtcTime_t *now_rtc, uint32_t now_ms);
void stage_render(const StageState_t *st, uint16_t *fb, const RtcTime_t *now_rtc, uint32_t now_ms);

#endif
