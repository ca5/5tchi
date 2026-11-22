### 

### たまごっちパラダイス 最小構成C言語プロジェクト技術仕様書（STAGE）

### たまごっちパラダイス 最小構成C言語プロジェクト技術仕様書（STAGE）

### 本仕様書は、たまごっちパラダイス（実機SoC: Sonix SNC73410）におけるベアメタルC言語ファームウェア開発の最小構成プロジェクト骨格、およびライブ進行支援ツール（STAGE）の完全な実装仕様を定義する技術文書である。

### 本体の内蔵フラッシュメモリを書き換えることなく内蔵SRAM上で一時実行可能なビルドシステムとハードウェア抽象化層（HAL）、誤操作防止機構を備えたメインループ実装、および安全な3段階の検証フローを網羅し、自作ファームウェア開発の立ち上げを支援する。

### ---

### 1\. ハードウェアアーキテクチャとシステム制約

### 自作ファームウェアの設計とビルド設定を成立させるためには、基板に搭載されているマイクロコントローラおよび周辺回路の電気的・論理的仕様を厳密に把握する必要がある。

### 1.1 プロセッサとメモリ空間

### たまごっちパラダイスには、Sonix製のマイクロコントローラ「SNC73410」が採用されている。コアアーキテクチャおよびメモリ構成は以下の仕様に従う。

* ### **MCUコア:** ARM Cortex-M3（ARMv7-Mアーキテクチャ、単精度・倍精度FPU非搭載）

* ### **動作クロック:** SysTickタイマー基準周波数 48 MHz（48,000カウントで1ミリ秒を生成）

* ### **内蔵SRAM:** 128 KiB（アドレス空間 0x20000000 〜 0x2001FFFF）

* ### **外部ストレージ:** 16 MiB SPI NORフラッシュ（XIP実行対応。初期ロットROMでは約5 MiB以上の未使用領域が存在）

* ### **時計機能:** リアルタイムクロック（RTC・カレンダー保持機能）内蔵

* ### **電源:** 単4形乾電池2本駆動

### SRAM配置ビルドでは、コード領域（.text）、読み出し専用データ領域（.rodata）、初期化済みデータ領域（.data）、未初期化データ領域（.bss）、およびスタック領域のすべてを内蔵SRAMの128 KiB空間内に収める構成を採る。

### 1.2 ディスプレイとグラフィックス仕様

### 視覚表示デバイスとして、128 × 128 ピクセルのカラー液晶モジュールが搭載されている。

* ### **解像度:** 横 128 ピクセル × 縦 128 ピクセル

* ### **色空間:** 16ビットカラー（RGB565フォーマット、各ピクセル2バイト）

* ### **フレームバッファ容量:** 128 × 128 × 2バイト ＝ 32,768バイト（32 KiB）

* ### **バス接続:** SPIシリアルバスを介して転送を実行

### RGB565形式では、上位5ビットが赤（Red）、中位6ビットが緑（Green）、下位5ビットが青（Blue）を表す。

### 1.3 操作デバイスと入力インターフェース

### 演奏中や暗所での操作性を考慮し、2系統の入力ハードウェアが備えられている。

* ### **正面プッシュボタンスイッチ:** 正面下部に配置された A、B、C の3つの独立ボタン。

* ### **右上側面ズームダイヤル:** 前後方向の回転を検出するロータリーエンコーダ、および独立した押し込みボタンスイッチを内蔵。

### 1.4 通信ポートと電気的制約

### デバッグおよびプログラム転送には、本体上部に露出している3極プロング端子（UART1）を使用する。

| 端子位置 | 信号線 | 接続先・規格 |
| ----- | ----- | ----- |
| **左プロング** | **UART1 RX** | USB-シリアル変換アダプタの TX 端子へ接続 |
| **中央プロング** | **GND** | USB-シリアル変換アダプタの GND 端子へ接続 |
| **右プロング** | **UART1 TX** | USB-シリアル変換アダプタの RX 端子へ接続 |

### UART周辺回路のレジスタベースアドレスは 0x4000B000 であり、通信速度は 460,800 baud で固定される。

### 接続するUSB-シリアル変換アダプタには厳格な制約が存在する。本体回路保護のため、必ず **3.3Vロジック** の汎用USB-UARTモジュール（FT232RL、FT232RQ、CH340G等）を使用しなければならない。5Vロジック信号の印加は内部回路破損を招くため禁止される。また、市販の完成品ドングルには汎用UARTモードとP-COM専用モードが存在するため、独自バイナリ転送には汎用UART対応品を選定する。

### ---

### 2\. ライブ進行ツール（STAGE）仕様とUI・操作設計

### STAGE は、ステージ演奏本番における時間超過や進行の乱れを防ぐため、「終了予定時刻（デッドライン）」を固定目標として現在時刻から逆算表示を行う専用ツールである。

### 2.1 画面レイアウト（128 × 128 RGB565）

### 画面は縦方向に3つの情報ブロックに分割され、直感的な視認性を確保する。

* ### **上段（絶対時刻ライン）:** 現在時刻（例: 現在 21:12）と目標デッドライン（例: 目標 21:30）を常時表示する。累積誤差の発生を防ぐため、内蔵RTCの現在時刻から目標時刻を引き算する差分算出方式を採用する。

* ### **中段（メイン逆算表示）:** 特大フォントにより残り時間を あと 18:00 の形式で表示する。残り時間に応じて外枠および背景色が遷移する：

  * ### **緑色:** 残り時間5分以上（正常進行）

  * ### **黄色:** 残り時間5分未満〜2分以上（警告領域）

  * ### **赤色点滅:** 残り時間2分未満（終演間近）

  * ### **赤色反転（例: \+00:35）:** 目標時刻を超過（超過時間をカウントアップ表示）

* ### **下段（巻き・押し収支＆無音メトロノーム）:** セットリスト予定進捗に対する「巻き・押し収支（例: 収支 \-01:20（押し））」を表示するとともに、セットリスト登録BPMに基づいた無音ビジュアルメトロノームを表示する。

  * ### **1拍目:** 画面全幅またはブロックの白／シアン色フラッシュ

  * ### **2〜4拍目:** 順次緑色ブロックの点灯表示

* ### **パート時間超過時のシーケンス:** 曲ごとの予定時間が終了しても自動で次の曲へ移行せず、演奏者の手動送り操作が行われるまで現在パートにとどまり、超過時間を赤色でカウントアップする。

### 2.2 本番中の操作割り当てと誤操作ガード

### 演奏中の振動や接触による誤動作を完全に排除するため、ボタン入力に対して厳格な優先順位とマスク処理が施されている。

| 操作入力 | 割り当て機能 | 判定条件および誤操作防止機構 |
| ----- | ----- | ----- |
| **ズームダイヤル短押し** | **計測開始 ／ 次のパートへ進む** | 押下時間 50ms〜800ms のリリース時に成立。回転直後 150ms 以内の押し込みは無効化（マスク処理） |
| **Cボタン短押し** | **1拍目の頭合わせ（位相リセット）** | BPM値は変更せず、押した瞬間を1拍目基準時刻として位相のみ即座にリセット（タップテンポ計測は行わない） |
| **ズームダイヤル回転** | **表示ビューのズーム切替** | 標準3段ビュー ↔ 巨大時計・残り時間ビュー ↔ ラップ履歴確認（または位相微調整）を切替 |
| **Aボタン・Bボタン短押し** | **入力無効（ガード）** | 正面左および中央の押しやすいボタンは、本番中の誤爆防止のため意図的に無効化 |
| **ズームダイヤル長押し** | **進行リカバリーメニュー呼出** | 2秒以上の押し込み継続でトグル開閉。パート戻し（PREV PART）、デッドライン調整（DEADLINE ±1m）、タイマーリセット、ランチャー復帰を提供 |

### 2.3 セットリスト入力パイプライン

### セットリストデータは、以下の2通りの方法で端末へ登録される。

* ### **本体単体入力:** 本体側面のズームダイヤルを回転させ、終了予定時刻、パート数、各パートの予定時間（30秒刻み）を直接入力する。

* ### **PC連携一括転送（Web Serial API）:** ChromeまたはEdgeブラウザ上のWebツールから、USB-UART（460,800 baud）を経由してCSV形式データ（番号,ラベル,予定秒数,BPM）およびPCの現在時刻を一括転送して同期する。

### ---

### 3\. 最小構成C言語プロジェクト骨格

### 外部ライブラリやランタイム依存を一切排除した独立ビルド環境 tama-stage-minimal のファイル群である。

### 3.1 ディレクトリ構成

```
tama-stage-minimal/
├── Makefile
├── linker.ld
├── startup.s
├── hal.h
├── hal.c
└── main.c
```

### 3.2 ビルド定義ファイル：Makefile

### ARM Cortex-M向けのGNUツールチェーン（arm-none-eabi-gcc）を用いて、SRAMロード用バイナリ tama\_stage.bin をビルドする。

```
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)as
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

CFLAGS  = -mcpu=cortex-m3 -mthumb -O2 -Wall -Wextra -ffreestanding -nostdlib
ASFLAGS = -mcpu=cortex-m3 -mthumb
LDFLAGS = -T linker.ld -nostdlib -Wl,--gc-sections

TARGET  = tama_stage

all: $(TARGET).bin

$(TARGET).elf: startup.s hal.c main.c linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) startup.s hal.c main.c -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(TARGET).elf $(TARGET).bin
```

### 3.3 リンカスクリプト：linker.ld

### SRAMアドレス 0x20000000 に全セクションを配置し、SRAMロードテストに特化したメモリレイアウトを定義する。スタックポインタ（\_estack）はSRAM末尾（128 KiB地点）に設定される。

```
ENTRY(Reset_Handler)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

### 3.4 スタートアップコード：startup.s

### ベクタテーブルの配置、未初期化データ領域（.bss）のゼロクリア、および main 関数への分岐を担うARM Thumbアセンブラコードである。

```
.syntax unified
.cpu cortex-m3
.thumb

.global g_pfnVectors
.global Reset_Handler

.section .isr_vector, "a", %progbits
.type g_pfnVectors, %object
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word Default_Handler
    .word Default_Handler
    .word 0
    .word Default_Handler
    .word SysTick_Handler

.section .text.Reset_Handler
.weak Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
zero_bss_loop:
    cmp r0, r1
    itt lt
    strlt r2, [r0], #4
    blt zero_bss_loop

    bl main

loop_forever:
    b loop_forever

.section .text.Default_Handler, "ax", %progbits
Default_Handler:
    b Default_Handler

.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
```

### ---

### 4\. ハードウェア抽象化層（HAL）

### 上位アプリケーションロジックとSNC73410固有のレジスタ操作を分離するHAL層の定義と実装である。

### 4.1 インターフェース定義：hal.h

### ディスプレイ寸法、主要色定数（RGB565）、ボタンビットマスク、および時刻保持構造体とAPI関数を宣言する。

```
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

#endif
```

### 4.2 ペリフェラル制御実装：hal.c

### SysTickタイマーレジスタ（アドレス 0xE000E010 〜 0xE000E018）を直接設定し、1ミリ秒精度のカウンタを駆動する。フレームバッファの実体確保および時刻変換関数を提供する。

```
#include "hal.h"

static uint16_t s_framebuffer[LCD_WIDTH * LCD_HEIGHT];
static volatile uint32_t s_tick_ms = 0;

#define SYSTICK_CSR   (*(volatile uint32_t *)0xE000E010)
#define SYSTICK_RVR   (*(volatile uint32_t *)0xE000E014)
#define SYSTICK_CVR   (*(volatile uint32_t *)0xE000E018)
#define UART1_BASE    0x4000B000

void SysTick_Handler(void) {
    s_tick_ms++;
}

void hal_init(void) {
    SYSTICK_RVR = 48000 - 1;
    SYSTICK_CVR = 0;
    SYSTICK_CSR = 0x07;
}

uint32_t hal_get_ms(void) {
    return s_tick_ms;
}

void hal_delay_ms(uint32_t ms) {
    uint32_t start = hal_get_ms();
    while ((hal_get_ms() - start) < ms) {
        __asm volatile ("nop");
    }
}

uint16_t* hal_lcd_get_framebuffer(void) {
    return s_framebuffer;
}

void hal_lcd_flush(void) {
    // SNC73410 SPI LCD 転送処理実装部
}

void hal_lcd_set_backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    (void)percent;
}

uint8_t hal_input_read_buttons(void) {
    return 0;
}

int8_t hal_input_read_dial_delta(void) {
    return 0;
}

void hal_rtc_get_time(RtcTime_t *out_time) {
    uint32_t total_sec = s_tick_ms / 1000;
    out_time->hour = (total_sec / 3600) % 24;
    out_time->min  = (total_sec / 60) % 60;
    out_time->sec  = total_sec % 60;
}
```

### ---

### 5\. アプリケーションメインロジック：main.c

### main.c は、入力イベントのフィルタリング、長押し・短押し判定、Cボタンによるメトロノーム拍頭リセット、および16ミリ秒（約60 FPS）間隔の画面再描画を実行する。

```
#include "hal.h"

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

int main(void) {
    hal_init();
    hal_lcd_set_backlight(60);

    uint16_t *fb = hal_lcd_get_framebuffer();
    uint8_t  prev_btns        = 0;
    uint32_t dial_press_start = 0;
    bool     dial_long_fired  = false;
    uint32_t last_rotate_ms   = 0;

    uint8_t  part_index       = 1;
    bool     menu_open        = false;
    uint32_t beat_origin_ms   = 0;
    const uint32_t beat_period_ms = 500;

    while (1) {
        uint32_t now_ms = hal_get_ms();
        uint8_t  btns   = hal_input_read_buttons();
        int8_t   dial   = hal_input_read_dial_delta();

        if (dial != 0) {
            last_rotate_ms = now_ms;
        }

        bool dial_down = (btns & BTN_DIAL) != 0;
        bool dial_prev = (prev_btns & BTN_DIAL) != 0;

        if (dial_down && !dial_prev) {
            dial_press_start = now_ms;
            dial_long_fired  = false;
        } else if (dial_down && !dial_long_fired) {
            if ((now_ms - dial_press_start) >= 2000) {
                dial_long_fired = true;
                menu_open = !menu_open;
            }
        } else if (!dial_down && dial_prev) {
            uint32_t held = now_ms - dial_press_start;
            if (!dial_long_fired && held >= 50 && (now_ms - last_rotate_ms) > 150) {
                if (!menu_open) {
                    part_index++;
                }
            }
        }

        bool c_down = (btns & BTN_C) != 0;
        bool c_prev = (prev_btns & BTN_C) != 0;
        if (c_down && !c_prev) {
            beat_origin_ms = now_ms;
        }

        prev_btns = btns;

        RtcTime_t rtc;
        hal_rtc_get_time(&rtc);

        draw_rect(fb, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);

        if (menu_open) {
            draw_border(fb, 6, COLOR_YELLOW);
            draw_rect(fb, 24, 48, 80, 32, COLOR_YELLOW);
        } else {
            uint32_t beat_elapsed = (now_ms - beat_origin_ms) % (beat_period_ms * 4);
            uint8_t  beat_idx     = (uint8_t)(beat_elapsed / beat_period_ms);
            uint32_t in_beat_ms   = beat_elapsed % beat_period_ms;

            if (beat_idx == 0 && in_beat_ms < 120) {
                draw_border(fb, 6, COLOR_WHITE);
            } else {
                draw_border(fb, 3, COLOR_GREEN);
            }

            for (int b = 0; b < 4; b++) {
                uint16_t box_col = (b == beat_idx)
                    ? ((b == 0) ? COLOR_CYAN : COLOR_GREEN)
                    : 0x2104;
                draw_rect(fb, 12 + b * 28, 92, 22, 20, box_col);
            }

            int sec_bar_w = (rtc.sec * 108) / 60;
            draw_rect(fb, 10, 14, sec_bar_w, 8, COLOR_GREEN);
        }

        hal_lcd_flush();
        hal_delay_ms(16);
    }

    return 0;
}
```

### ---

### 6\. 安全な開発・検証ワークフロー

### 実機のSPI NORフラッシュを破損（文鎮化）させることなく、安全に動作確認を進めるための3段階の検証フローである。

### 6.1 ステップ1：PCエミュレータ「Capybara」での論理検証

### Rust製SNC73410エミュレータ infinition/capybara を用い、PC上でELF/バイナリの基本動作を確認する。

* ### 128 × 128 RGB565 フレームバッファの描画整合性

* ### A / B / C ボタンおよびダイヤル回転・押し込みイベントの伝達

* ### SysTickタイマーおよび仮想RTCの時間経過処理

### 6.2 ステップ2：USB-UART経由の内蔵SRAM一時ロード実行

### 実機を用いた初期テストでは、フラッシュメモリ書き換えを一切行わず、USB-UART経由で生成バイナリ（tama\_stage.bin）を内蔵SRAMアドレス 0x20000000 に直接転送して一時実行する。

* ### **リスクゼロの保証:** 本体の電池を抜いて再起動すれば即座に純正ROM状態へ復帰するため、文鎮化のリスクが存在しない。

* ### **確認項目:** 実機液晶での発色・視野角、バックライト輝度（60%）、ダイヤル回転直後の押し込みマスク（150ms）による操作感の検証。

### 6.3 ステップ3：SPI NORフラッシュへのスタンドアロン書き込み

### SRAMロードテストにより完全な動作が確認された後、外部SPI NORフラッシュの未使用領域へ書き込みを行う。XIP実行時におけるフラッシュ書き換えの制限（消去・書き込みルーチンは内蔵SRAM .ramfunc に配置し、処理中は割り込みを禁止すること）を遵守して本番ファームウェア化を完了させる。

### 

