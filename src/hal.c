#include "hal.h"

static uint16_t s_framebuffer[LCD_WIDTH * LCD_HEIGHT];
static volatile uint32_t s_tick_ms = 0;

#define REG(a) (*(volatile uint32_t *)(a))
#define BB(reg, bit) (*(volatile uint32_t *)(0x42000000u + (((uint32_t)(reg) - 0x40000000u) << 5) + ((uint32_t)(bit) << 2)))

/* ARM Cortex-M3 System Control / SysTick / NVIC */
#define SCB_VTOR      REG(0xE000ED08u)
#define SYSTICK_CSR   REG(0xE000E010u)
#define SYSTICK_RVR   REG(0xE000E014u)
#define SYSTICK_CVR   REG(0xE000E018u)
#define NVIC_ICER(n)  REG(0xE000E180u + ((uint32_t)(n) * 4u))
#define NVIC_ICPR(n)  REG(0xE000E280u + ((uint32_t)(n) * 4u))

/* Sonix SNC73410 Clock / Timer */
#define CLK_BASE      0x45000000u
#define CT2_BASE      0x40002000u

/* GPIO Ports */
#define GPIO0_BASE    0x40018000u
#define GPIO1_BASE    0x40019000u
#define GPIO2_BASE    0x4001A000u

#define LCD_RST(v)    (BB(GPIO0_BASE + 0x00u, 4) = (v))
#define LCD_DC(v)     (BB(GPIO0_BASE + 0x00u, 5) = (v))

#define PIN_BTN_DIAL  (1u << 8)
#define PIN_BTN_A     (1u << 9)
#define PIN_BTN_C     (1u << 10)
#define PIN_BTN_B     (1u << 11)
#define MASK_DIAL_ENC 0x03u

/* Panel SPI (0x4000E000) */
#define PANEL_BASE    0x4000E000u
#define SPI_CTRL      REG(PANEL_BASE + 0x00u)
#define SPI_STAT      REG(PANEL_BASE + 0x0Cu)
#define SPI_DATA      REG(PANEL_BASE + 0x1Cu)
#define SPIWAIT       200000u

/* UART1 (0x4000B000, 460800 baud @ 96 MHz) */
#define UART1_BASE    0x4000B000u
#define UART1_RBR_DLL REG(UART1_BASE + 0x00u)
#define UART1_IER_DLM REG(UART1_BASE + 0x04u)
#define UART1_FCR     REG(UART1_BASE + 0x08u)
#define UART1_LCR     REG(UART1_BASE + 0x0Cu)
#define UART1_LSR     REG(UART1_BASE + 0x14u)
#define UART1_FDR     REG(UART1_BASE + 0x28u)
#define UART1_CTRL    REG(UART1_BASE + 0x30u)
#define UART1_LSR_RDR (1u << 0)
#define UART1_LSR_THRE (1u << 5)

static void uart1_putc(char c) {
    for (uint32_t g = 0; (UART1_LSR & UART1_LSR_THRE) == 0u && g < 100000u; g++) {}
    UART1_RBR_DLL = (uint8_t)c;
}

static void uart1_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart1_putc('\r');
        uart1_putc(*s++);
    }
}

#define UART_RX_LINE_MAX 128
#define UART_RING_SIZE   256
static char     s_uart_line[UART_RX_LINE_MAX];
static int      s_uart_len = 0;
static char     s_uart_ring[UART_RING_SIZE];
static volatile uint16_t s_ring_head = 0;
static volatile uint16_t s_ring_tail = 0;
static uint8_t  s_enc_prev = 0;
static bool     s_bl_inited = false;
static bool     s_lcd_sleeping = false;
static uint32_t s_rtc_base_unix = 0;
static uint32_t s_rtc_base_cnt  = 0;

#define HW_SEC_COUNTER  REG(0x45000304u)
#define PARA_MB         ((volatile uint32_t *)0x20000FC0u)
#define PARA_MB_MAGIC   0x4B4C4350u

static void udelay(volatile uint32_t n) {
    while (n--) __asm volatile ("nop");
}
#define MS_BUSY(x) udelay((uint32_t)(x) * 16000u)

static void wdt_feed(void) {
    REG(0x4000800Cu) = 0x5AFA55AAu;
    REG(0x4000900Cu) = 0x5AFA55AAu;
}

static void wdt_off(void) {
    REG(0x4000900Cu) = 0x5AFA55AAu;
    REG(0x40009000u) = 0x5AFA0000u;
    REG(0x40008000u) = 0x5AFA0000u;
    REG(0x40008004u) = 0x5AFA0002u;
    REG(0x40008008u) = 0x5AFA00FFu;
    REG(0x4000800Cu) = 0x5AFA55AAu;
}

void SysTick_Handler(void) {
    s_tick_ms++;
}

static void clock_init(void) {
    REG(CLK_BASE + 0x00u) |= 1u;
    REG(CLK_BASE + 0x0Cu) = 0u;
    REG(CLK_BASE + 0x00u) = 0x41u;
    {
        uint32_t r = (REG(CLK_BASE + 0x34u) & 0xF00u) | 0xA55A0000u;
        REG(CLK_BASE + 0x04u) = r;
    }
    REG(CLK_BASE + 0x18u) = 0x21u;
    REG(CLK_BASE + 0x50u) = 0u;
    REG(CLK_BASE + 0x10Cu) = 0x001FFFFDu;
    REG(CLK_BASE + 0x110u) = 0u;
    REG(CLK_BASE + 0x108u) = 0u;
    REG(CLK_BASE + 0x10u)  = 0u;
    REG(CLK_BASE + 0x00u) |= 0x10u;
    while (!((REG(CLK_BASE + 0x08u) >> 4) & 1u)) {}
    REG(CLK_BASE + 0x00u) |= 0x4000u;
    REG(CLK_BASE + 0x04u) &= ~1u;
    REG(CLK_BASE + 0x00u) |= 0x8000u;
    while (!((REG(CLK_BASE + 0x08u) >> 6) & 1u)) {}
    {
        uint32_t r = (REG(CLK_BASE + 0x0Cu) & ~7u) | 4u;
        REG(CLK_BASE + 0x0Cu) = r;
    }
    while (((REG(CLK_BASE + 0x0Cu) >> 4) & 7u) != (REG(CLK_BASE + 0x0Cu) & 7u)) {}
    REG(CLK_BASE + 0x100u) = 0x00157EB7u;
}

static void pin_set(uint8_t id, uint8_t mode) {
    *(volatile uint8_t *)(0xE000E400u + id) = (uint8_t)(mode << 4);
}

static void pin_mux(void) {
    static const uint8_t m12[] = {
        6, 7, 8, 9, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x19, 0x1f, 0x20, 0x22,
        0x24, 0x25, 0x2e, 0x2f, 0x35, 0x36, 0x1a, 0x1b, 0x1c, 0x26, 0x27, 0x28,
        0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x3a, 0x3f
    };
    static const uint8_t m8[] = { 0, 1, 2, 3 };
    static const uint8_t m4[] = { 0x15, 0x17 };
    for (unsigned i = 0; i < sizeof(m12); i++) pin_set(m12[i], 0xCu);
    for (unsigned i = 0; i < sizeof(m8); i++)  pin_set(m8[i], 0x8u);
    for (unsigned i = 0; i < sizeof(m4); i++)  pin_set(m4[i], 0x4u);
}

static void uart1_init(void) {
    UART1_LCR     = 0x83u;
    UART1_RBR_DLL = 0x1Au; /* 460800 baud @ 96 MHz */
    UART1_IER_DLM = 0x00u;
    UART1_LCR     = 0x03u;
    UART1_FDR     = 0x100u;
    UART1_FCR     = 0x07u;
    UART1_CTRL    = 0xC1u;
}

static void display_power(void) {
    static const uint32_t seq[][2] = {
        {0x45000020,0x40},{0x45000210,0x0},{0x45000214,0x0},{0x45000218,0x0},
        {0x45000204,0x0},{0x45000200,0xe0},{0x45000204,0x0},{0x45000200,0xe0},
        {0x45000020,0x42},{0x4003a000,0x8000},{0x4003a004,0x8000},{0x4003a004,0x8001},
        {0x40038000,0x0},{0x40038014,0xf0},{0x40038018,0xa001},{0x40038014,0xf0},
        {0x40038004,0x18005da0},{0x40038008,0xffc},{0x4003800c,0x1},{0x40038010,0x1},
        {0x40038000,0x10},{0x40038000,0x0},{0x40038014,0xf0},{0x40038018,0xa001},
        {0x40038014,0xf0},{0x40038004,0x18005da0},{0x40038008,0xffc},{0x4003800c,0x1},
        {0x40038010,0x1},{0x40038000,0x10},{0x40038000,0x0},{0x40038014,0xf0},
        {0x40038018,0xa001},{0x40038014,0xf0},{0x40038004,0x18000ba0},{0x40038008,0xffc},
        {0x4003800c,0x1},{0x40038010,0x1},{0x40038000,0x10},{0x40038000,0x0},
        {0x40007034,0x0},{0x40038000,0x0},{0x40038014,0xf0},{0x40038018,0xa001},
        {0x40038014,0xf0},{0x40038004,0x18000ba0},{0x40038008,0xffc},{0x4003800c,0x1},
        {0x40038010,0x1},{0x40038000,0x10},{0x40038000,0x0},{0x40038014,0xf0},
        {0x40038018,0xa001},{0x40038014,0xf0},{0x40038004,0x18005da0},{0x40038008,0xffc},
        {0x4003800c,0x1},{0x40038010,0x1},{0x40038000,0x10},{0x40038000,0x0},
        {0x40038014,0xf0},{0x40038018,0xa001},{0x40038014,0xf0},{0x40038004,0x18005da0},
        {0x40038008,0xffc},{0x4003800c,0x1},{0x40038010,0x1},{0x40038000,0x10},
        {0x40038000,0x0},{0x4001803c,0xffffffff},{0x4001903c,0xfffff5df},{0x4001a03c,0xffffffff},
        {0x40018004,0x30},{0x40018008,0x15008055},
        {0x40019004,0xa20},{0x40019008,0x55995800},
        {0x4001a004,0x0},{0x4001a008,0x50},
        {0x40003000,0x0},{0x40003018,0x0},{0x4000301c,0x0},{0x40003020,0x0},
        {0x40003024,0x0},{0x40003014,0x0},{0x40003000,0x2},{0x40003008,0x0},
        {0x4000303c,0x1f},{0x40003000,0x1}
    };
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        uint32_t a = seq[i][0], v = seq[i][1];
        if (a >= 0x45000210u && a <= 0x45000218u) {
            v = REG(0x4001803Cu + ((a - 0x45000210u) >> 2) * 0x1000u);
        }
        REG(a) = v;
    }
    LCD_RST(1); BB(GPIO0_BASE + 0x04u, 4) = 1;
    LCD_DC(0);  BB(GPIO0_BASE + 0x04u, 5) = 1;
    BB(GPIO1_BASE + 0x00u, 5) = 0; BB(GPIO1_BASE + 0x04u, 5) = 1;
    BB(GPIO1_BASE + 0x00u, 9) = 0; BB(GPIO1_BASE + 0x04u, 9) = 1;
    MS_BUSY(20);
}

static void spi_init(void) {
    REG(PANEL_BASE + 0x08u) = 1u;
    SPI_CTRL &= ~(1u << 18);
    REG(PANEL_BASE + 0x04u) &= ~6u;
    REG(PANEL_BASE + 0x04u) &= ~1u;
    SPI_CTRL &= ~0x10u;
    SPI_CTRL &= ~0x08u;
    REG(PANEL_BASE + 0x20u) = 0u;
    SPI_CTRL = (SPI_CTRL & ~0xF00u) | 0x700u; /* 8-bit */
    REG(PANEL_BASE + 0x24u) |= 0x80000000u;
    REG(PANEL_BASE + 0x24u) &= ~0xFF0000u;
    REG(PANEL_BASE + 0x24u) &= ~0xFFu;
    SPI_CTRL |= 1u;
    SPI_CTRL |= 0xC0u;
}

static void spi_tx(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t g = 0; (SPI_STAT & 2u) && g < SPIWAIT; g++) {}
        SPI_DATA = p[i];
    }
    udelay(8);
    for (uint32_t g = 0; (SPI_STAT & 0x10u) && g < SPIWAIT; g++) {}
    SPI_CTRL |= 0xC0u;
}

static void lcd_cmd(uint8_t c, const uint8_t *par, uint32_t n) {
    LCD_DC(0);
    spi_tx(&c, 1);
    if (n) {
        LCD_DC(1);
        spi_tx(par, n);
        LCD_DC(0);
    }
}

static void display_init(void) {
    static const uint8_t p36[] = {0x08}, p3a[] = {0x05}, p26[] = {0x01};
    static const uint8_t pb1[] = {0x08, 0x3b, 0x3b}, pb2[] = {0x08, 0x3b, 0x3b};
    static const uint8_t pb3[] = {0x08, 0x3b, 0x3b, 0x08, 0x3b, 0x3b}, pb4[] = {0x03};
    static const uint8_t pc0[] = {0xe0, 0x00, 0x84}, pc1[] = {0xc4};
    static const uint8_t pc2[] = {0x0d, 0x00}, pc3[] = {0x8d, 0x2a}, pc4[] = {0x8d, 0xee}, pc5[] = {0x06};
    static const uint8_t pe0[] = {0x0d,0x17,0x16,0x28,0x3f,0x39,0x30,0x31,0x2c,0x28,0x2f,0x39,0x00,0x04,0x01,0x10};
    static const uint8_t pe1[] = {0x0a,0x15,0x18,0x25,0x3f,0x37,0x2e,0x30,0x2f,0x2c,0x33,0x3f,0x00,0x03,0x04,0x10};
    static const uint8_t p35[] = {0x00}, pcas0[] = {0x00,0x02,0x00,0x83}, pras0[] = {0x00,0x01,0x00,0x81};

    spi_init();
    LCD_RST(1); MS_BUSY(10);
    LCD_RST(0); MS_BUSY(20);
    LCD_RST(1); MS_BUSY(150);

    lcd_cmd(0x11, 0, 0); MS_BUSY(150);
    lcd_cmd(0x36, p36, 1); lcd_cmd(0x3a, p3a, 1); lcd_cmd(0x26, p26, 1); lcd_cmd(0x20, 0, 0);
    lcd_cmd(0xb1, pb1, 3); lcd_cmd(0xb2, pb2, 3); lcd_cmd(0xb3, pb3, 6); lcd_cmd(0xb4, pb4, 1);
    lcd_cmd(0xc0, pc0, 3); lcd_cmd(0xc1, pc1, 1); lcd_cmd(0xc2, pc2, 2); lcd_cmd(0xc3, pc3, 2);
    lcd_cmd(0xc4, pc4, 2); lcd_cmd(0xc5, pc5, 1);
    lcd_cmd(0xe0, pe0, 16); lcd_cmd(0xe1, pe1, 16);
    lcd_cmd(0x35, p35, 1);
    lcd_cmd(0x29, 0, 0); MS_BUSY(20);
    lcd_cmd(0x2a, pcas0, 4); lcd_cmd(0x2b, pras0, 4);
    lcd_cmd(0x11, 0, 0); MS_BUSY(150);
}

static void pwm2(uint32_t mr1, int first) {
    if (first) REG(CT2_BASE + 0x34u) = 0u;
    REG(CT2_BASE + 0x00u) = 0u;
    REG(CT2_BASE + 0x18u) = 0u; REG(CT2_BASE + 0x1Cu) = 0u;
    REG(CT2_BASE + 0x20u) = 0u; REG(CT2_BASE + 0x24u) = 0u;
    REG(CT2_BASE + 0x14u) = 0u;
    REG(CT2_BASE + 0x00u) = 2u; REG(CT2_BASE + 0x08u) = 0u; REG(CT2_BASE + 0x00u) = 0u;
    REG(CT2_BASE + 0x14u) = 0x400u;
    REG(CT2_BASE + 0x24u) = 0x17700u;
    REG(CT2_BASE + 0x1Cu) = mr1;
    REG(CT2_BASE + 0x14u) = 0u;
    REG(CT2_BASE + 0x34u) = 0x00200002u;
    REG(CT2_BASE + 0x00u) = 2u;
    REG(CT2_BASE + 0x3Cu) = 0x1Fu;
    REG(CT2_BASE + 0x00u) = 1u;
}

static void flashctl_init(void) {
    /* Enable XIP window (0x10000000 -> flash 0x11000) as in Paramix-Studio hw.c */
    REG(0x4002F004u) = 0x60011000u;
    REG(0x4002F000u) = 0x00000003u;
    REG(0x40022000u) = 0x0000d018u;
    REG(0x40022000u) = 0x0000d000u;
    REG(0x40022000u) = 0x0000d080u;
    REG(0x40022000u) = 0x0000d088u;
    REG(0x40022008u) = 0x00003077u;
    REG(0x4002200Cu) = 0x0000f000u;
    REG(0x40022004u) = 0x00000400u;
    REG(0x40022004u) = 0x00004000u;
}

static int32_t para_days(int32_t y, int32_t m, int32_t d) {
    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static uint32_t para_mailbox_read(uint32_t cnt, uint32_t max_age) {
    volatile uint32_t *m = PARA_MB;
    if (m[0] != PARA_MB_MAGIC || m[3] != ~m[1] || !m[1] || (cnt - m[2]) > max_age) {
        return 0;
    }
    return m[1] + (cnt - m[2]);
}

static uint32_t para_flash_read(uint32_t cnt) {
    for (uint32_t pg = 0x60EFE000u; pg <= 0x60EFF000u; pg += 0x1000u) {
        const volatile uint8_t *b = (const volatile uint8_t *)pg;
        uint32_t c = 0;
        for (uint32_t i = 4; i < 0x1000u; i++) {
            c ^= b[i];
            for (int k = 0; k < 8; k++) c = (c & 1u) ? ((c >> 1) ^ 0xA001u) : (c >> 1);
        }
        if ((b[0] | ((uint32_t)b[1] << 8)) != c) continue;
        if ((b[2] | ((uint32_t)b[3] << 8)) != (~c & 0xFFFFu)) continue;

        uint32_t days = b[0x0C] | ((uint32_t)b[0x0D] << 8) | ((uint32_t)b[0x0E] << 16) | ((uint32_t)b[0x0F] << 24);
        uint32_t sek  = b[0x10] | ((uint32_t)b[0x11] << 8) | ((uint32_t)b[0x12] << 16) | ((uint32_t)b[0x13] << 24);
        if (days > 40000u || sek >= 86400u) continue;

        uint32_t y  = b[0x1C] | ((uint32_t)b[0x1D] << 8);
        uint32_t mo = b[0x1E] | ((uint32_t)b[0x1F] << 8);
        uint32_t d  = b[0x20] | ((uint32_t)b[0x21] << 8);
        uint32_t hh = b[0x22] | ((uint32_t)b[0x23] << 8);
        uint32_t mi = b[0x24] | ((uint32_t)b[0x25] << 8);
        uint32_t se = b[0x26] | ((uint32_t)b[0x27] << 8);
        if (y < 2000u || y > 2100u || mo < 1u || mo > 12u || d < 1u || d > 31u || hh > 23u || mi > 59u || se > 59u) {
            continue;
        }
        uint32_t t = (20089u + days) * 86400u + sek + cnt;
        uint32_t f = (uint32_t)para_days((int32_t)y, (int32_t)mo, (int32_t)d) * 86400u + hh * 3600u + mi * 60u + se;
        if (f > t + 600u || (t - f) > 366u * 86400u) continue;
        return t;
    }
    return 0;
}

static void rtc_hw_init(void) {
    uint32_t cnt = HW_SEC_COUNTER;
    uint32_t t = para_mailbox_read(cnt, 120u);
    if (!t) t = para_flash_read(cnt);
    if (!t) t = para_mailbox_read(cnt, 86400u);
    s_rtc_base_unix = t;
    s_rtc_base_cnt  = cnt;
    if (t) uart1_puts("[DBG] rtc=ok\n");
    else   uart1_puts("[DBG] rtc=fallback\n");
}

void hal_init(void) {
    __asm volatile ("cpsid i" ::: "memory");

    SYSTICK_CSR = 0u;
    for (uint32_t i = 0; i < 4u; i++) {
        NVIC_ICER(i) = 0xFFFFFFFFu;
        NVIC_ICPR(i) = 0xFFFFFFFFu;
    }
    SCB_VTOR = 0x00000000u;
    __asm volatile ("dsb\nisb" ::: "memory");

    wdt_off();
    uart1_puts("[DBG] 1:wdt_off\n");
    clock_init();
    uart1_puts("[DBG] 2:clock_init\n");
    REG(CLK_BASE + 0x20u) = 0x40u;
    pin_mux();
    uart1_init();
    uart1_puts("[DBG] 3:pin_mux+uart\n");
    flashctl_init();
    rtc_hw_init();
    display_power();
    uart1_puts("[DBG] 4:display_power\n");
    display_init();
    uart1_puts("[DBG] 5:display_init\n");

    REG(GPIO0_BASE + 0x04u) &= ~(PIN_BTN_DIAL | PIN_BTN_A | PIN_BTN_C | PIN_BTN_B);
    REG(GPIO2_BASE + 0x04u) &= ~MASK_DIAL_ENC;
    s_enc_prev = (uint8_t)(REG(GPIO2_BASE + 0x00u) & MASK_DIAL_ENC);

    SYSTICK_RVR = 96000u - 1u; /* 1 ms @ 96 MHz */
    SYSTICK_CVR = 0u;
    SYSTICK_CSR = 0x07u;

    __asm volatile ("cpsie i" ::: "memory");
    uart1_puts("[DBG] 6:hal_init_done\n");
}

uint32_t hal_get_ms(void) {
    return s_tick_ms;
}

static void uart1_poll_rx(void) {
    while ((UART1_LSR & UART1_LSR_RDR) != 0u) {
        char c = (char)(UART1_RBR_DLL & 0xFFu);
        uint16_t next = (uint16_t)((s_ring_head + 1u) % UART_RING_SIZE);
        if (next != s_ring_tail) {
            s_uart_ring[s_ring_head] = c;
            s_ring_head = next;
        }
    }
}

void hal_delay_ms(uint32_t ms) {
    uint32_t start = hal_get_ms();
    while ((hal_get_ms() - start) < ms) {
        wdt_feed();
        uart1_poll_rx();
        __asm volatile ("wfi");
    }
}

uint16_t* hal_lcd_get_framebuffer(void) {
    return s_framebuffer;
}

void hal_lcd_flush(void) {
    static const uint8_t pcas[] = {0x00, 0x02, 0x00, 0x81};
    static const uint8_t pras[] = {0x00, 0x01, 0x00, 0x80};
    wdt_feed();
    lcd_cmd(0x2a, pcas, 4);
    lcd_cmd(0x2b, pras, 4);
    {
        uint8_t c = 0x2c;
        LCD_DC(0);
        spi_tx(&c, 1);
    }
    LCD_DC(1);
    for (uint32_t i = 0; i < (uint32_t)(LCD_WIDTH * LCD_HEIGHT); i++) {
        if ((i & 0xFFu) == 0u) uart1_poll_rx();
        uint16_t p = s_framebuffer[i];
        uint8_t bytes[2] = { (uint8_t)(p >> 8), (uint8_t)(p & 0xFFu) };
        spi_tx(&bytes[0], 1);
        spi_tx(&bytes[1], 1);
    }
    LCD_DC(0);
}

void hal_lcd_set_backlight(uint8_t percent) {
    if (percent > 100u) percent = 100u;
    uint32_t mr1 = (percent == 0u) ? 0x17700u : 0u;
    if (!s_bl_inited) {
        REG(0x40007034u) = 0u;
        pwm2(0x17700u, 1);
        REG(0x40009000u) = 0x5AFA0000u;
        REG(0x40008000u) = 0x5AFA0000u;
        REG(0x40018034u) = 0x15555155u;
        REG(0x40019034u) = 0x55540140u;
        pwm2(mr1, 0);
        REG(CLK_BASE + 0x20u) = 0x42u;
        s_bl_inited = true;
    } else {
        pwm2(mr1, 0);
    }
}

void hal_lcd_sleep(bool sleep) {
    if (sleep == s_lcd_sleeping) return;
    if (sleep) {
        REG(CT2_BASE + 0x00u) = 0u;
        REG(CT2_BASE + 0x34u) = 0u;
        lcd_cmd(0x28u, 0, 0); /* DISPOFF */
        lcd_cmd(0x10u, 0, 0); /* SLPIN */
        s_bl_inited = false;
        s_lcd_sleeping = true;
    } else {
        lcd_cmd(0x11u, 0, 0); /* SLPOUT */
        hal_delay_ms(125u);
        lcd_cmd(0x29u, 0, 0); /* DISPON */
        hal_delay_ms(20u);
        s_lcd_sleeping = false;
        hal_lcd_set_backlight(60u);
    }
}

uint8_t hal_input_read_buttons(void) {
    uint32_t p0 = REG(GPIO0_BASE + 0x00u);
    uint8_t btns = 0;
    if ((p0 & PIN_BTN_A) == 0u)    btns |= BTN_A;
    if ((p0 & PIN_BTN_B) == 0u)    btns |= BTN_B;
    if ((p0 & PIN_BTN_C) == 0u)    btns |= BTN_C;
    if ((p0 & PIN_BTN_DIAL) == 0u) btns |= BTN_DIAL;
    return btns;
}

int8_t hal_input_read_dial_delta(void) {
    static const int8_t quad_lut[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };
    uint8_t cur = (uint8_t)(REG(GPIO2_BASE + 0x00u) & MASK_DIAL_ENC);
    uint8_t idx = (uint8_t)((s_enc_prev << 2) | cur);
    s_enc_prev = cur;
    return quad_lut[idx];
}

void hal_rtc_get_time(RtcTime_t *out_time) {
    if (s_rtc_base_unix != 0u) {
        uint32_t now_sec = s_rtc_base_unix + (HW_SEC_COUNTER - s_rtc_base_cnt);
        uint32_t tod = now_sec % 86400u;
        out_time->hour = (uint8_t)(tod / 3600u);
        out_time->min  = (uint8_t)((tod % 3600u) / 60u);
        out_time->sec  = (uint8_t)(tod % 60u);
        return;
    }
    uint32_t total_sec = s_tick_ms / 1000u;
    out_time->hour = (uint8_t)((total_sec / 3600u) % 24u);
    out_time->min  = (uint8_t)((total_sec / 60u) % 60u);
    out_time->sec  = (uint8_t)(total_sec % 60u);
}

int hal_uart_read_line(char *buf, int max_len) {
    if (!buf || max_len <= 1) return 0;

    uart1_poll_rx();
    while (s_ring_tail != s_ring_head) {
        char c = s_uart_ring[s_ring_tail];
        s_ring_tail = (uint16_t)((s_ring_tail + 1u) % UART_RING_SIZE);
        if (c == '\r' || c == '\n') {
            if (s_uart_len > 0) {
                int out_len = (s_uart_len < max_len - 1) ? s_uart_len : (max_len - 1);
                for (int i = 0; i < out_len; i++) {
                    buf[i] = s_uart_line[i];
                }
                buf[out_len] = '\0';
                s_uart_len = 0;
                return out_len;
            }
        } else if (s_uart_len < UART_RX_LINE_MAX - 1) {
            s_uart_line[s_uart_len++] = c;
        }
    }
    return 0;
}

void hal_debug_log(const char *s) {
    uart1_puts(s);
}

void hal_system_reboot(void) {
    /* Back to the Paradise as in Paramix-Studio hw_back_to_paradise() */
    __asm volatile ("cpsid i" ::: "memory");
    pin_mux();
    flashctl_init();
    ((void (*)(void))(0x100FF568u | 1u))();
    for (;;) {
        __asm volatile ("nop");
    }
}
