# Molecular for the Pocket Master: the device firmware. Run through the toolchain container:
#   ../../toolchain/nds make -C device
# Flags as the MVsilicon SDK's examples (Example_GPIO/Debug/makefile), -O2 for the audio.
# A diagnostic build: make DIAG=-DDIAG_STEP_MS=10000 (the progress meter, hal.c).
# MVsilicon's BP10xx SDK, not in this tree: make SDK=... (no comment on the line itself: its spaces would join the value)
SDK ?= $(abspath ../../sdk/bp1048_sdk/MVsB1_Base_SDK)
CC := nds32le-elf-gcc
OUT := ../build/device
ARCH := -mcmodel=medium -mcpu=d1088-spu -mext-dsp -mext-zol   # the v3f toolchain: ABI2 FP+, as the SDK libraries
CFLAGS := -O2 -g3 -Wall $(ARCH) -fmessage-length=0 -fsingle-precision-constant -ffunction-sections -fdata-sections \
          -I$(SDK)/driver/driver/inc -I$(SDK)/driver/driver_api/inc -I$(SDK)/middleware/mv_utils/inc \
          -I. -I../core -I../build -DHOST=1 -DFUNC_OS_EN=0 \
          -DUSER_DEFINE_MCLK_112896M_DIV_VALUE=11289600 -DUSER_DEFINE_MCLK_12288M_DIV_VALUE=12288000 $(DIAG)
LDFLAGS := -O2 -nostartfiles -static $(ARCH) -L$(SDK)/driver/driver -T app.ld -Wl,--gc-sections -mvh -Wl,-Map,$(OUT)/molecular.map
SRCS := main.c hal.c usbmidi.c armlink.c startup/init.c $(SDK)/startup/interrupt.c $(SDK)/startup/retarget.c \
        $(SDK)/driver/driver_api/src/dac_interface.c \
        $(SDK)/driver/driver_api/src/adc_interface.c
OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(notdir $(SRCS))) $(OUT)/obj/crt0.o
vpath %.c . startup $(SDK)/startup $(SDK)/driver/driver_api/src

all: $(OUT)/molecular.bin

$(OUT)/obj/%.o: %.c
	@mkdir -p $(OUT)/obj
	$(CC) $(CFLAGS) -c $< -o $@
# main.c includes the whole core (core/molecular.c): rebuilt when any of it changes (build 70)
$(OUT)/obj/main.o: $(wildcard ../core/*.c ../core/*.h ../core/vendor/*/*.c ../core/vendor/*/*.h ../core/vendor/*/*/*.c ../core/vendor/*/*/*.h)
$(OUT)/obj/crt0.o: $(SDK)/startup/crt0.S
	@mkdir -p $(OUT)/obj
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/molecular.adx: $(OBJS) app.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lDriver -ldsp
	@# build 73: the FPU is single precision only: any double-precision instruction is a crash on the pedal
	@if nds32le-elf-objdump -d $@ | grep -qE '\s(fs2d|fd2s|fsi2d|fui2d|fd2u?iz?|faddd|fsubd|fmuld|fdivd|fsqrtd|fcmp[a-z]+d|fabsd|fcpyn?sd|fmfdr|fmtdr|fn?m(add|sub)d|fcmov[nz]d)\s'; then \
		echo "Molecular: double-precision FPU instructions in the image"; rm -f $@; exit 1; fi
$(OUT)/molecular.bin: $(OUT)/molecular.adx
	nds32le-elf-objcopy -S -O binary $< $@
	nds32le-elf-size $<

clean:
	rm -rf $(OUT)
.PHONY: all clean
