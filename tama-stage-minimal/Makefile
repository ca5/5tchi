PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)as
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

CFLAGS  = -mcpu=cortex-m3 -mthumb -O2 -Wall -Wextra -ffreestanding -nostdlib
ASFLAGS = -mcpu=cortex-m3 -mthumb
LDFLAGS = -T linker.ld -nostdlib -Wl,--gc-sections

TARGET  = tama_stage

.PHONY: all clean sim

all: $(TARGET).bin

$(TARGET).elf: startup.s hal.c font.c stage.c main.c linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) startup.s hal.c font.c stage.c main.c -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(TARGET).elf $(TARGET).bin sim/libtama_sim.dylib

sim/libtama_sim.dylib: main.c font.c stage.c sim/hal_sim.c hal.h font.h stage.h
	clang -arch arm64 -arch x86_64 -O2 -Wall -Wextra -shared -fPIC -Dmain=app_main main.c font.c stage.c sim/hal_sim.c -o $@

sim: sim/libtama_sim.dylib
	-pkill -f "[s]im/sim.py" >/dev/null 2>&1 || true
	python3 sim/sim.py
