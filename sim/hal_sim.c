#include "hal.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint16_t s_framebuffer[LCD_WIDTH * LCD_HEIGHT];
static uint16_t s_frontbuffer[LCD_WIDTH * LCD_HEIGHT];
static struct timespec s_start_ts;

static atomic_uint_fast8_t  s_sim_buttons   = 0;
static atomic_int_fast8_t   s_sim_dial      = 0;
static atomic_uint_fast32_t s_frame_count   = 0;
static atomic_uint_fast8_t  s_backlight_pct = 100;

#define UART_QUEUE_CAP 32
#define UART_LINE_MAX  128
static pthread_mutex_t s_uart_mu = PTHREAD_MUTEX_INITIALIZER;
static char s_uart_queue[UART_QUEUE_CAP][UART_LINE_MAX];
static int  s_uart_head  = 0;
static int  s_uart_tail  = 0;
static int  s_uart_count = 0;

void hal_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &s_start_ts);
}

uint32_t hal_get_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t sec  = (int64_t)(now.tv_sec - s_start_ts.tv_sec);
    int64_t nsec = (int64_t)(now.tv_nsec - s_start_ts.tv_nsec);
    return (uint32_t)(sec * 1000 + nsec / 1000000);
}

void hal_delay_ms(uint32_t ms) {
    usleep((useconds_t)ms * 1000);
}

uint16_t* hal_lcd_get_framebuffer(void) {
    return s_framebuffer;
}

void hal_lcd_flush(void) {
    memcpy(s_frontbuffer, s_framebuffer, sizeof(s_framebuffer));
    atomic_fetch_add(&s_frame_count, 1);
}

void hal_lcd_set_backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    atomic_store(&s_backlight_pct, percent);
}

uint8_t hal_input_read_buttons(void) {
    return (uint8_t)atomic_load(&s_sim_buttons);
}

int8_t hal_input_read_dial_delta(void) {
    return (int8_t)atomic_exchange(&s_sim_dial, 0);
}

void hal_rtc_get_time(RtcTime_t *out_time) {
    time_t t = time(NULL);
    struct tm tm_now;
    localtime_r(&t, &tm_now);
    out_time->hour = (uint8_t)tm_now.tm_hour;
    out_time->min  = (uint8_t)tm_now.tm_min;
    out_time->sec  = (uint8_t)tm_now.tm_sec;
}

int hal_uart_read_line(char *buf, int max_len) {
    if (!buf || max_len <= 1) return 0;
    int len = 0;
    pthread_mutex_lock(&s_uart_mu);
    if (s_uart_count > 0) {
        const char *src = s_uart_queue[s_uart_head];
        while (len < max_len - 1 && src[len] != '\0') {
            buf[len] = src[len];
            len++;
        }
        buf[len] = '\0';
        s_uart_head = (s_uart_head + 1) % UART_QUEUE_CAP;
        s_uart_count--;
    }
    pthread_mutex_unlock(&s_uart_mu);
    return len;
}

void hal_debug_log(const char *s) {
    (void)s;
}

void hal_system_reboot(void) {
    /* No-op in host simulator */
}

/* Host simulator bridge API */
void sim_set_buttons(uint8_t mask) {
    atomic_store(&s_sim_buttons, mask);
}

void sim_add_dial_delta(int8_t delta) {
    atomic_fetch_add(&s_sim_dial, delta);
}

void sim_push_uart_line(const char *line) {
    if (!line) return;
    pthread_mutex_lock(&s_uart_mu);
    if (s_uart_count < UART_QUEUE_CAP) {
        int i = 0;
        while (i < UART_LINE_MAX - 1 && line[i] != '\0') {
            s_uart_queue[s_uart_tail][i] = line[i];
            i++;
        }
        s_uart_queue[s_uart_tail][i] = '\0';
        s_uart_tail = (s_uart_tail + 1) % UART_QUEUE_CAP;
        s_uart_count++;
    }
    pthread_mutex_unlock(&s_uart_mu);
}

uint32_t sim_get_frame_count(void) {
    return (uint32_t)atomic_load(&s_frame_count);
}

uint8_t sim_get_backlight(void) {
    return (uint8_t)atomic_load(&s_backlight_pct);
}

const uint16_t* sim_get_frontbuffer(void) {
    return s_frontbuffer;
}
