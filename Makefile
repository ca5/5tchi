PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)as
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

CFLAGS  = -mcpu=cortex-m3 -mthumb -O2 -Wall -Wextra -ffreestanding -nostdlib -Isrc
ASFLAGS = -mcpu=cortex-m3 -mthumb
LDFLAGS = -T src/linker.ld -nostdlib -Wl,--gc-sections

BUILD   = build
TARGET  = $(BUILD)/tama_stage
SRCS    = src/startup.s src/hal.c src/font.c src/stage.c src/main.c
SIM_SRC = src/main.c src/font.c src/stage.c sim/hal_sim.c

.PHONY: all clean sim

all: $(TARGET).bin

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET).elf: $(SRCS) src/linker.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD) sim/libtama_sim.dylib

sim/libtama_sim.dylib: $(SIM_SRC) src/hal.h src/font.h src/stage.h
	clang -arch arm64 -arch x86_64 -O2 -Wall -Wextra -shared -fPIC -Isrc -Dmain=app_main $(SIM_SRC) -o $@

sim: sim/libtama_sim.dylib
	-pkill -f "[s]im/sim.py" >/dev/null 2>&1 || true
	python3 sim/sim.py
