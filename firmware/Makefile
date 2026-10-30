# OPENMPK3 firmware build
TARGET  = openmpk3
BUILD   = build
CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size
TUSB    = lib/tinyusb/src
VERSION ?= 02.00

SRCS = $(wildcard src/*.c) $(wildcard core/*.c) \
       $(TUSB)/tusb.c $(TUSB)/common/tusb_fifo.c $(TUSB)/device/usbd.c $(TUSB)/device/usbd_control.c \
       $(TUSB)/class/midi/midi_device.c $(TUSB)/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
INC  = -Isrc -Icore -I$(TUSB) -Ilib/cmsis_device_f0/Include -Ilib/cmsis_core
CFLAGS = -mcpu=cortex-m0 -mthumb -Os -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
         -ffunction-sections -fdata-sections -DSTM32F070xB -MMD -MP $(INC)
LDFLAGS = -mcpu=cortex-m0 -mthumb -T ld/app.ld -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
          --specs=nano.specs --specs=nosys.specs -nostartfiles
OBJS = $(patsubst %.c,$(BUILD)/%.o,$(SRCS))

all: $(BUILD)/$(TARGET)_full.bin
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/$(TARGET).elf: $(OBJS) ld/app.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@
$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/$(TARGET)_full.bin: $(BUILD)/$(TARGET).bin ../tools/mkimage.py
	cd .. && python3 tools/mkimage.py firmware/$< firmware/$@ --version $(VERSION)

-include $(OBJS:.o=.d)

test:
	@mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Icore test/test_core.c core/*.c -o $(BUILD)/test_core && $(BUILD)/test_core
clean:
	rm -rf $(BUILD)
.PHONY: all test clean
