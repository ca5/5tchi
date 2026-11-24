#include "stage.h"
#include "font.h"

static void draw_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= LCD_HEIGHT) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= LCD_WIDTH) continue;
            fb[j * LCD_WIDTH + i] = color;
        }
    }
}

static void draw_border(uint16_t *fb, int thickness, uint16_t color) {
    draw_rect(fb, 0, 0, LCD_WIDTH, thickness, color);
    draw_rect(fb, 0, LCD_HEIGHT - thickness, LCD_WIDTH, thickness, color);
    draw_rect(fb, 0, 0, thickness, LCD_HEIGHT, color);
    draw_rect(fb, LCD_WIDTH - thickness, 0, thickness, LCD_HEIGHT, color);
}

static void fmt_2d(char *dst, int32_t v) {
    if (v < 0) v = -v;
    dst[0] = (char)('0' + (v / 10) % 10);
    dst[1] = (char)('0' + (v % 10));
}

static void fmt_3d(char *dst, int32_t v) {
    if (v < 0) v = -v;
    dst[0] = (char)('0' + (v / 100) % 10);
    dst[1] = (char)('0' + (v / 10) % 10);
    dst[2] = (char)('0' + (v % 10));
}

static void fmt_hhmm(char *dst, int32_t sec_of_day) {
    int32_t s = sec_of_day % 86400;
    if (s < 0) s += 86400;
    fmt_2d(&dst[0], s / 3600);
    dst[2] = ':';
    fmt_2d(&dst[3], (s % 3600) / 60);
    dst[5] = '\0';
}

static void fmt_mmss(char *dst, int32_t total_sec) {
    int32_t s = total_sec < 0 ? -total_sec : total_sec;
    int32_t mins = s / 60;
    if (mins > 99) mins = 99;
    fmt_2d(&dst[0], mins);
    dst[2] = ':';
    fmt_2d(&dst[3], s % 60);
    dst[5] = '\0';
}

static void fmt_signed_mmss(char *dst, int32_t signed_sec) {
    dst[0] = (signed_sec < 0) ? '-' : '+';
    fmt_mmss(&dst[1], signed_sec);
}

static void copy_label(char *dst, const char *src, int max_len) {
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int32_t effective_rtc_sec(const StageState_t *st, const RtcTime_t *t) {
    int32_t raw = (int32_t)t->hour * 3600 + (int32_t)t->min * 60 + (int32_t)t->sec;
    int32_t s = (raw + st->rtc_offset_sec) % 86400;
    if (s < 0) s += 86400;
    return s;
}

static int32_t deadline_remaining_sec(const StageState_t *st, const RtcTime_t *now_rtc) {
    int32_t now_s = effective_rtc_sec(st, now_rtc);
    int32_t diff = st->deadline_sec_of_day - now_s;
    if (diff < -43200) diff += 86400;
    if (diff > 43200) diff -= 86400;
    return diff;
}

static void draw_centered_str(uint16_t *fb, int y, const char *s, uint16_t col, uint8_t scale) {
    int w = font_measure_str(s, scale);
    font_draw_str(fb, (LCD_WIDTH - w) / 2, y, s, col, scale);
}

static int32_t total_planned_sec(const StageState_t *st) {
    int32_t total = 0;
    for (uint8_t i = 0; i < st->part_count; i++) {
        total += st->parts[i].planned_sec;
    }
    return total;
}

static void reset_run_state(StageState_t *st, const RtcTime_t *now_rtc, uint32_t now_ms, bool recalc_deadline) {
    st->current_part = 0;
    st->running = false;
    st->finished = false;
    st->cumulative_diff_sec = 0;
    st->part_start_ms = now_ms;
    st->beat_origin_ms = now_ms;
    for (uint8_t i = 0; i < STAGE_MAX_PARTS; i++) {
        st->actual_sec[i] = 0;
    }
    if (recalc_deadline) {
        st->deadline_sec_of_day = (effective_rtc_sec(st, now_rtc) + total_planned_sec(st)) % 86400;
    }
}

void stage_init(StageState_t *st, const RtcTime_t *now_rtc, uint32_t now_ms) {
    static const char *default_labels[4] = {
        "01 INTRO", "02 MAIN", "03 BRIDGE", "04 OUTRO"
    };
    static const uint16_t default_secs[4] = { 180, 300, 240, 360 };
    static const uint16_t default_bpms[4] = { 120, 128, 115, 132 };

    st->part_count = 4;
    for (uint8_t i = 0; i < STAGE_MAX_PARTS; i++) {
        if (i < 4) {
            copy_label(st->parts[i].label, default_labels[i], STAGE_LABEL_LEN);
            st->parts[i].planned_sec = default_secs[i];
            st->parts[i].bpm = default_bpms[i];
        } else {
            copy_label(st->parts[i].label, "PART", STAGE_LABEL_LEN);
            st->parts[i].label[4] = (char)('1' + i);
            st->parts[i].label[5] = '\0';
            st->parts[i].planned_sec = 180;
            st->parts[i].bpm = 120;
        }
    }

    st->rtc_offset_sec = 0;
    st->view = VIEW_STANDARD;
    st->menu_open = false;
    st->menu_cursor = MENU_PREV_PART;
    st->setup_open = false;
    st->setup_cursor = 0;
    st->setup_editing = false;
    st->setup_part_open = false;
    st->setup_part_field = 0;
    st->setup_name_pos = 0;
    st->uart_has_explicit_end = false;

    reset_run_state(st, now_rtc, now_ms, true);
}

static uint8_t setup_item_count(const StageState_t *st) {
    return (uint8_t)(2 + st->part_count + 1); /* END, PARTS, P1..Pn, DONE */
}

static void execute_menu_item(StageState_t *st, const RtcTime_t *now_rtc, uint32_t now_ms) {
    switch (st->menu_cursor) {
        case MENU_PREV_PART:
            if (st->current_part > 0) {
                st->current_part--;
                st->cumulative_diff_sec -=
                    (st->actual_sec[st->current_part] - (int32_t)st->parts[st->current_part].planned_sec);
                st->actual_sec[st->current_part] = 0;
                st->part_start_ms = now_ms;
                st->finished = false;
            }
            break;
        case MENU_DEADLINE_PLUS:
            st->deadline_sec_of_day = (st->deadline_sec_of_day + 60) % 86400;
            break;
        case MENU_DEADLINE_MINUS:
            st->deadline_sec_of_day = (st->deadline_sec_of_day + 86400 - 60) % 86400;
            break;
        case MENU_SETUP_SETLIST:
            st->setup_open = true;
            st->setup_cursor = 0;
            st->setup_editing = false;
            st->setup_part_open = false;
            st->setup_part_field = 0;
            st->setup_name_pos = 0;
            break;
        case MENU_RESET_TIMER:
            reset_run_state(st, now_rtc, now_ms, true);
            break;
        case MENU_EXIT_REBOOT:
            hal_system_reboot();
            break;
        case MENU_CLOSE:
        default:
            break;
    }
    st->menu_open = false;
}

void stage_on_dial_short_press(StageState_t *st, const RtcTime_t *now_rtc, uint32_t now_ms) {
    if (st->setup_open) {
        if (st->setup_part_open) {
            if (st->setup_part_field == 3) {
                st->setup_part_open = false;
                st->setup_editing = false;
            } else if (st->setup_part_field == 0) {
                if (!st->setup_editing) {
                    st->setup_editing = true;
                    st->setup_name_pos = 0;
                } else if (st->setup_name_pos + 1 < 8) {
                    st->setup_name_pos++;
                } else {
                    st->setup_editing = false;
                }
            } else {
                st->setup_editing = !st->setup_editing;
            }
            return;
        }
        uint8_t done_idx = (uint8_t)(2 + st->part_count);
        if (st->setup_cursor == done_idx) {
            st->setup_open = false;
            st->setup_editing = false;
            reset_run_state(st, now_rtc, now_ms, false);
        } else if (st->setup_cursor >= 2) {
            st->setup_part_open = true;
            st->setup_part_field = 0;
            st->setup_editing = false;
        } else {
            st->setup_editing = !st->setup_editing;
        }
        return;
    }
    if (st->menu_open) {
        execute_menu_item(st, now_rtc, now_ms);
        return;
    }
    if (!st->running) {
        st->running = true;
        st->finished = false;
        st->part_start_ms = now_ms;
        return;
    }
    if (st->finished) return;

    int32_t elapsed = (int32_t)((now_ms - st->part_start_ms) / 1000);
    st->actual_sec[st->current_part] = elapsed;
    st->cumulative_diff_sec += (elapsed - (int32_t)st->parts[st->current_part].planned_sec);

    if (st->current_part + 1 < st->part_count) {
        st->current_part++;
        st->part_start_ms = now_ms;
    } else {
        st->finished = true;
    }
}

static char step_label_char(char cur, int dir) {
    static const char charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    const int n = (int)(sizeof(charset) - 1);
    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (charset[i] == cur) {
            idx = i;
            break;
        }
    }
    idx = (idx + dir + n) % n;
    return charset[idx];
}

void stage_on_dial_rotate(StageState_t *st, int8_t delta) {
    if (delta == 0) return;
    int dir = (delta > 0) ? 1 : -1;

    if (st->setup_open) {
        if (st->setup_part_open) {
            uint8_t pi = (uint8_t)(st->setup_cursor - 2);
            if (!st->setup_editing) {
                int f = (int)st->setup_part_field + dir;
                if (f < 0) f = 3;
                if (f > 3) f = 0;
                st->setup_part_field = (uint8_t)f;
            } else if (st->setup_part_field == 0) {
                char *lbl = st->parts[pi].label;
                uint8_t pos = st->setup_name_pos;
                if (lbl[pos] == '\0') {
                    lbl[pos] = ' ';
                    lbl[pos + 1] = '\0';
                }
                lbl[pos] = step_label_char(lbl[pos], dir);
            } else if (st->setup_part_field == 1) {
                int32_t sec = (int32_t)st->parts[pi].planned_sec + dir;
                if (sec < 1) sec = 1;
                if (sec > 3600) sec = 3600;
                st->parts[pi].planned_sec = (uint16_t)sec;
            } else if (st->setup_part_field == 2) {
                int32_t bpm = (int32_t)st->parts[pi].bpm + dir;
                if (bpm < 40) bpm = 40;
                if (bpm > 240) bpm = 240;
                st->parts[pi].bpm = (uint16_t)bpm;
            }
            return;
        }

        uint8_t count = setup_item_count(st);
        if (!st->setup_editing) {
            int cur = (int)st->setup_cursor + dir;
            if (cur < 0) cur = count - 1;
            if (cur >= count) cur = 0;
            st->setup_cursor = (uint8_t)cur;
        } else if (st->setup_cursor == 0) {
            st->deadline_sec_of_day = (st->deadline_sec_of_day + 86400 + dir * 60) % 86400;
        } else if (st->setup_cursor == 1) {
            int pc = (int)st->part_count + dir;
            if (pc < 1) pc = 1;
            if (pc > STAGE_MAX_PARTS) pc = STAGE_MAX_PARTS;
            st->part_count = (uint8_t)pc;
        }
        return;
    }

    if (st->menu_open) {
        int cur = (int)st->menu_cursor + dir;
        if (cur < 0) cur = MENU_ITEM_COUNT - 1;
        if (cur >= MENU_ITEM_COUNT) cur = 0;
        st->menu_cursor = (StageMenuItem_t)cur;
        return;
    }

    int v = (int)st->view + dir;
    if (v < 0) v = VIEW_COUNT - 1;
    if (v >= VIEW_COUNT) v = 0;
    st->view = (StageView_t)v;
}

void stage_on_beat_reset(StageState_t *st, uint32_t now_ms) {
    st->beat_origin_ms = now_ms;
}

void stage_on_cancel(StageState_t *st) {
    if (st->setup_open) {
        if (st->setup_editing) {
            st->setup_editing = false;
        } else if (st->setup_part_open) {
            st->setup_part_open = false;
        } else {
            st->setup_open = false;
        }
        return;
    }
    if (st->menu_open) {
        st->menu_open = false;
    }
}

void stage_toggle_menu(StageState_t *st) {
    if (st->setup_open) {
        if (st->setup_part_open) {
            st->setup_part_open = false;
            st->setup_editing = false;
            return;
        }
        st->setup_open = false;
        st->setup_editing = false;
        return;
    }
    st->menu_open = !st->menu_open;
}

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return false;
        s++;
        prefix++;
    }
    return true;
}

static const char* parse_int_field(const char *s, int32_t *out) {
    int32_t val = 0;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    *out = neg ? -val : val;
    return s;
}

static bool parse_hhmmss_str(const char *s, int32_t *out_sec, bool has_sec) {
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9' || s[2] != ':') return false;
    if (s[3] < '0' || s[3] > '9' || s[4] < '0' || s[4] > '9') return false;
    int32_t h = (s[0] - '0') * 10 + (s[1] - '0');
    int32_t m = (s[3] - '0') * 10 + (s[4] - '0');
    int32_t sec = 0;
    if (has_sec) {
        if (s[5] != ':' || s[6] < '0' || s[6] > '9' || s[7] < '0' || s[7] > '9') return false;
        sec = (s[6] - '0') * 10 + (s[7] - '0');
    }
    if (h >= 24 || m >= 60 || sec >= 60) return false;
    *out_sec = h * 3600 + m * 60 + sec;
    return true;
}

void stage_process_uart_line(StageState_t *st, const char *line, const RtcTime_t *now_rtc, uint32_t now_ms) {
    if (!line || !*line) return;

    if (starts_with(line, "CLEAR")) {
        st->part_count = 0;
        st->uart_has_explicit_end = false;
        return;
    }
    if (starts_with(line, "TIME ")) {
        int32_t target_s = 0;
        if (parse_hhmmss_str(line + 5, &target_s, true)) {
            int32_t raw_s = (int32_t)now_rtc->hour * 3600 + (int32_t)now_rtc->min * 60 + (int32_t)now_rtc->sec;
            st->rtc_offset_sec = (target_s - raw_s + 86400) % 86400;
        }
        return;
    }
    if (starts_with(line, "END ")) {
        int32_t end_s = 0;
        if (parse_hhmmss_str(line + 4, &end_s, false)) {
            st->deadline_sec_of_day = end_s;
            st->uart_has_explicit_end = true;
        }
        return;
    }
    if (starts_with(line, "APPLY")) {
        if (st->part_count == 0) st->part_count = 1;
        reset_run_state(st, now_rtc, now_ms, !st->uart_has_explicit_end);
        st->menu_open = false;
        st->setup_open = false;
        return;
    }

    /* CSV row: index,label,planned_sec,bpm */
    const char *p = line;
    if (starts_with(p, "PART,")) p += 5;
    if (*p < '0' || *p > '9') return;

    int32_t idx = 0, sec = 180, bpm = 120;
    p = parse_int_field(p, &idx);
    if (*p != ',') return;
    p++;

    char lbl[STAGE_LABEL_LEN];
    int li = 0;
    while (*p && *p != ',') {
        if (li < STAGE_LABEL_LEN - 1) lbl[li++] = *p;
        p++;
    }
    lbl[li] = '\0';
    if (*p != ',') return;
    p++;

    p = parse_int_field(p, &sec);
    if (*p == ',') {
        p++;
        parse_int_field(p, &bpm);
    }

    if (sec < 1) sec = 1;
    if (sec > 3600) sec = 3600;
    if (bpm < 40) bpm = 40;
    if (bpm > 240) bpm = 240;

    uint8_t slot = st->part_count;
    if (idx >= 1 && idx <= STAGE_MAX_PARTS) {
        slot = (uint8_t)(idx - 1);
    }
    if (slot >= STAGE_MAX_PARTS) return;

    copy_label(st->parts[slot].label, lbl[0] ? lbl : "PART", STAGE_LABEL_LEN);
    st->parts[slot].planned_sec = (uint16_t)sec;
    st->parts[slot].bpm = (uint16_t)bpm;
    if (slot + 1 > st->part_count) {
        st->part_count = (uint8_t)(slot + 1);
    }
}

static void render_menu(const StageState_t *st, uint16_t *fb) {
    static const char *labels[MENU_ITEM_COUNT] = {
        "PREV PART",
        "DEADLINE +1M",
        "DEADLINE -1M",
        "SETUP SETLIST",
        "RESET TIMER",
        "EXIT (REBOOT)",
        "CLOSE MENU"
    };
    draw_border(fb, 4, COLOR_YELLOW);
    draw_centered_str(fb, 7, "RECOVERY MENU", COLOR_YELLOW, 1);
    draw_rect(fb, 8, 16, 112, 1, COLOR_YELLOW);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int y = 21 + i * 15;
        bool sel = (i == (int)st->menu_cursor);
        if (sel) {
            draw_rect(fb, 8, y - 2, 112, 11, COLOR_YELLOW);
            font_draw_str(fb, 12, y, labels[i], COLOR_BLACK, 1);
        } else {
            font_draw_str(fb, 12, y, labels[i], COLOR_WHITE, 1);
        }
    }
}

static void render_setup_part(const StageState_t *st, uint16_t *fb) {
    uint8_t pi = (uint8_t)(st->setup_cursor - 2);
    char hdr[] = "PART 1 EDIT";
    hdr[5] = (char)('1' + pi);

    draw_border(fb, 3, COLOR_CYAN);
    draw_centered_str(fb, 7, hdr, COLOR_CYAN, 1);
    draw_rect(fb, 6, 17, 116, 1, COLOR_CYAN);

    for (int f = 0; f < 4; f++) {
        int y = 26 + f * 20;
        bool sel = (f == (int)st->setup_part_field);
        char line[24];
        if (f == 0) {
            copy_label(line, "NAME ", 6);
            copy_label(line + 5, st->parts[pi].label, STAGE_LABEL_LEN);
        } else if (f == 1) {
            char mmss[6];
            fmt_mmss(mmss, st->parts[pi].planned_sec);
            copy_label(line, "TIME ", 6);
            copy_label(line + 5, mmss, 6);
        } else if (f == 2) {
            char bpm3[4];
            fmt_3d(bpm3, st->parts[pi].bpm);
            copy_label(line, "BPM  ", 6);
            copy_label(line + 5, bpm3, 4);
        } else {
            copy_label(line, "BACK", 5);
        }

        if (sel) {
            uint16_t bg = st->setup_editing ? COLOR_GREEN : COLOR_CYAN;
            draw_rect(fb, 6, y - 2, 116, 12, bg);
            font_draw_str(fb, 10, y + 1, line, COLOR_BLACK, 1);
            if (f == 0 && st->setup_editing) {
                int cx = 10 + (5 + st->setup_name_pos) * FONT_ADVANCE_X;
                draw_rect(fb, cx, y + 9, FONT_GLYPH_W, 2, COLOR_BLACK);
            }
        } else {
            font_draw_str(fb, 10, y + 1, line, COLOR_WHITE, 1);
        }
    }

    font_draw_str(fb, 8, 114, st->setup_editing ? "A/C/ROT:VAL B:OK" : "A/C/ROT:FLD B:ED", 0x7BEF, 1);
}

static void render_setup(const StageState_t *st, uint16_t *fb) {
    if (st->setup_part_open) {
        render_setup_part(st, fb);
        return;
    }

    draw_border(fb, 3, COLOR_CYAN);
    draw_centered_str(fb, 7, "SETLIST SETUP", COLOR_CYAN, 1);
    draw_rect(fb, 6, 17, 116, 1, COLOR_CYAN);

    uint8_t total = setup_item_count(st);
    int top = 0;
    if (st->setup_cursor >= 5) {
        top = (int)st->setup_cursor - 4;
    }

    for (int row = 0; row < 5; row++) {
        int idx = top + row;
        if (idx >= total) break;
        int y = 24 + row * 17;
        bool sel = (idx == (int)st->setup_cursor);

        char line[24];
        if (idx == 0) {
            char hhmm[6];
            fmt_hhmm(hhmm, st->deadline_sec_of_day);
            copy_label(line, "END   ", 7);
            copy_label(line + 6, hhmm, 6);
        } else if (idx == 1) {
            copy_label(line, "PARTS ", 7);
            line[6] = (char)('0' + st->part_count);
            line[7] = '\0';
        } else if (idx < 2 + st->part_count) {
            uint8_t pi = (uint8_t)(idx - 2);
            char mmss[6], bpm3[4];
            fmt_mmss(mmss, st->parts[pi].planned_sec);
            fmt_3d(bpm3, st->parts[pi].bpm);
            line[0] = 'P';
            line[1] = (char)('1' + pi);
            line[2] = ' ';
            int li = 0;
            while (li < 5 && st->parts[pi].label[li] != '\0') {
                line[3 + li] = st->parts[pi].label[li];
                li++;
            }
            while (li < 5) {
                line[3 + li] = ' ';
                li++;
            }
            line[8] = ' ';
            copy_label(line + 9, mmss, 6);
            line[14] = ' ';
            copy_label(line + 15, bpm3, 4);
        } else {
            copy_label(line, "DONE (APPLY)", 13);
        }

        if (sel) {
            uint16_t bg = st->setup_editing ? COLOR_GREEN : COLOR_CYAN;
            draw_rect(fb, 6, y - 2, 116, 12, bg);
            font_draw_str(fb, 10, y + 1, line, COLOR_BLACK, 1);
        } else {
            font_draw_str(fb, 10, y + 1, line, COLOR_WHITE, 1);
        }
    }

    font_draw_str(fb, 8, 114, st->setup_editing ? "A/C/ROT:VAL B:OK" : "A/C/ROT:SEL B:ED", 0x7BEF, 1);
}

static void render_metronome(const StageState_t *st, uint16_t *fb, uint32_t now_ms, uint16_t border_col) {
    uint16_t bpm = (st->part_count > 0) ? st->parts[st->current_part].bpm : 120;
    if (bpm == 0) bpm = 120;
    uint32_t period_ms = 60000u / bpm;
    uint32_t elapsed = (now_ms - st->beat_origin_ms) % (period_ms * 4u);
    uint8_t  beat_idx = (uint8_t)(elapsed / period_ms);
    uint32_t in_beat = elapsed % period_ms;

    if (beat_idx == 0 && in_beat < 110) {
        draw_border(fb, 5, COLOR_WHITE);
    } else {
        draw_border(fb, 3, border_col);
    }

    for (int b = 0; b < 4; b++) {
        uint16_t box_col = (b == beat_idx)
            ? ((b == 0) ? COLOR_CYAN : COLOR_GREEN)
            : 0x2104;
        draw_rect(fb, 12 + b * 28, 102, 22, 16, box_col);
    }
}

static void render_standard_view(const StageState_t *st, uint16_t *fb, const RtcTime_t *now_rtc, uint32_t now_ms) {
    int32_t rem_sec = deadline_remaining_sec(st, now_rtc);
    bool blink_on = ((now_ms / 350u) % 2u) == 0;

    uint16_t status_col = COLOR_GREEN;
    if (rem_sec < 0) {
        status_col = COLOR_RED;
    } else if (rem_sec < 120) {
        status_col = blink_on ? COLOR_RED : COLOR_BLACK;
    } else if (rem_sec < 300) {
        status_col = COLOR_YELLOW;
    }

    char now_buf[6], end_buf[6];
    fmt_hhmm(now_buf, effective_rtc_sec(st, now_rtc));
    fmt_hhmm(end_buf, st->deadline_sec_of_day);
    font_draw_str(fb, 8, 9, "NOW", COLOR_CYAN, 1);
    font_draw_str(fb, 28, 9, now_buf, COLOR_WHITE, 1);
    font_draw_str(fb, 66, 9, "END", COLOR_YELLOW, 1);
    font_draw_str(fb, 86, 9, end_buf, COLOR_WHITE, 1);
    draw_rect(fb, 6, 19, 116, 1, 0x3186);

    char cd_buf[8];
    if (rem_sec < 0) {
        fmt_signed_mmss(cd_buf, -rem_sec);
        draw_rect(fb, 6, 26, 116, 32, COLOR_RED);
        draw_centered_str(fb, 32, cd_buf, COLOR_BLACK, 3);
    } else {
        fmt_mmss(cd_buf, rem_sec);
        uint16_t fg = (rem_sec < 120) ? COLOR_RED : status_col;
        draw_centered_str(fb, 32, cd_buf, fg, 3);
    }

    const StagePart_t *p = &st->parts[st->current_part];
    font_draw_str(fb, 8, 66, p->label, COLOR_CYAN, 1);

    int32_t part_rem = (int32_t)p->planned_sec;
    if (st->running) {
        part_rem -= (int32_t)((now_ms - st->part_start_ms) / 1000u);
    }
    char prem_buf[8];
    if (part_rem < 0) {
        fmt_signed_mmss(prem_buf, -part_rem);
        font_draw_str(fb, 84, 66, prem_buf, COLOR_RED, 1);
    } else {
        fmt_mmss(prem_buf, part_rem);
        font_draw_str(fb, 90, 66, prem_buf, st->running ? COLOR_WHITE : 0x7BEF, 1);
    }

    char diff_buf[8];
    fmt_signed_mmss(diff_buf, st->cumulative_diff_sec);
    font_draw_str(fb, 8, 82, "DIFF", 0xAD55, 1);
    uint16_t diff_col = (st->cumulative_diff_sec > 0) ? COLOR_RED : COLOR_GREEN;
    font_draw_str(fb, 36, 82, diff_buf, diff_col, 1);
    font_draw_str(fb, 84, 82, st->running ? "RUN" : "WAIT", st->running ? COLOR_GREEN : COLOR_YELLOW, 1);

    render_metronome(st, fb, now_ms, status_col);
}

static void render_big_clock_view(const StageState_t *st, uint16_t *fb, const RtcTime_t *now_rtc, uint32_t now_ms) {
    int32_t rem_sec = deadline_remaining_sec(st, now_rtc);
    uint16_t col = (rem_sec < 0 || rem_sec < 120) ? COLOR_RED : (rem_sec < 300 ? COLOR_YELLOW : COLOR_GREEN);

    char now_buf[6];
    fmt_hhmm(now_buf, effective_rtc_sec(st, now_rtc));
    draw_centered_str(fb, 14, now_buf, COLOR_WHITE, 2);

    char cd_buf[8];
    if (rem_sec < 0) {
        fmt_signed_mmss(cd_buf, -rem_sec);
        draw_rect(fb, 6, 48, 116, 40, COLOR_RED);
        draw_centered_str(fb, 58, cd_buf, COLOR_BLACK, 3);
    } else {
        fmt_mmss(cd_buf, rem_sec);
        draw_centered_str(fb, 54, cd_buf, col, 4);
    }

    draw_centered_str(fb, 90, "BIG CLOCK", 0x7BEF, 1);
    render_metronome(st, fb, now_ms, col);
}

static void render_lap_history_view(const StageState_t *st, uint16_t *fb) {
    draw_border(fb, 3, COLOR_CYAN);
    draw_centered_str(fb, 8, "LAP HISTORY", COLOR_CYAN, 1);
    draw_rect(fb, 8, 18, 112, 1, COLOR_CYAN);

    uint8_t show = st->part_count < 4 ? st->part_count : 4;
    for (uint8_t i = 0; i < show; i++) {
        int y = 26 + i * 20;
        font_draw_str(fb, 8, y, st->parts[i].label, (i == st->current_part) ? COLOR_YELLOW : COLOR_WHITE, 1);

        char act_buf[6], diff_buf[8];
        if (i < st->current_part || st->finished) {
            fmt_mmss(act_buf, st->actual_sec[i]);
            int32_t d = st->actual_sec[i] - (int32_t)st->parts[i].planned_sec;
            fmt_signed_mmss(diff_buf, d);
            font_draw_str(fb, 76, y, act_buf, COLOR_WHITE, 1);
            font_draw_str(fb, 76, y + 9, diff_buf, d > 0 ? COLOR_RED : COLOR_GREEN, 1);
        } else {
            fmt_mmss(act_buf, st->parts[i].planned_sec);
            font_draw_str(fb, 76, y, act_buf, 0x7BEF, 1);
        }
    }
}

void stage_render(const StageState_t *st, uint16_t *fb, const RtcTime_t *now_rtc, uint32_t now_ms) {
    draw_rect(fb, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);

    if (st->setup_open) {
        render_setup(st, fb);
        return;
    }
    if (st->menu_open) {
        render_menu(st, fb);
        return;
    }

    switch (st->view) {
        case VIEW_BIG_CLOCK:
            render_big_clock_view(st, fb, now_rtc, now_ms);
            break;
        case VIEW_LAP_HISTORY:
            render_lap_history_view(st, fb);
            break;
        case VIEW_STANDARD:
        default:
            render_standard_view(st, fb, now_rtc, now_ms);
            break;
    }
}
