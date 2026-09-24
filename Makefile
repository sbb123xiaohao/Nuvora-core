SHELL := /bin/sh
ARCH ?= x86_64
ifeq ($(filter $(ARCH),x86_64 aarch64),)
$(error ARCH must be x86_64 or aarch64; 32-bit x86 is no longer supported)
endif
ifeq ($(ARCH),aarch64)
CROSS ?= aarch64-linux-gnu-
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
PYTHON ?= python3
BUILD := build/aarch64
ARM_CFLAGS := -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -mgeneral-regs-only -fno-asynchronous-unwind-tables -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -MMD -MP
ARM_OBJS := $(BUILD)/arch/aarch64/boot.o $(BUILD)/arch/aarch64/neon.o $(BUILD)/arch/aarch64/user.o $(BUILD)/arch/aarch64/vectors.o $(BUILD)/arch/aarch64/kernel.o $(BUILD)/common/string.o
.DELETE_ON_ERROR:
.PHONY: all run test clean check
all: $(BUILD)/Image
$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) -ffreestanding -g -c $< -o $@
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ARM_CFLAGS) -c $< -o $@
$(BUILD)/nuvora.elf: $(ARM_OBJS) arch/aarch64/linker.ld
	$(LD) -T arch/aarch64/linker.ld -Map $(BUILD)/nuvora.map -o $@ $(ARM_OBJS)
$(BUILD)/Image: $(BUILD)/nuvora.elf
	$(OBJCOPY) -O binary $< $@
run: all
	$(PYTHON) scripts/arm64.py run
test: all
	$(PYTHON) scripts/arm64.py test
check: test
clean:
	rm -rf $(BUILD)
-include $(ARM_OBJS:.o=.d)
else
CROSS ?=
CC := $(CROSS)gcc
LD := $(CROSS)ld
PYTHON ?= python3
BUILD := build/$(ARCH)
export NV_ARCH := $(ARCH)
OBJCOPY := $(CROSS)objcopy
ARCHDIR := x86_64
MACHINE := elf_x86_64
ARCHFLAGS := -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only
ULINK := user/linker64.ld
USTART := start64
UEXTRA := $(BUILD)/user/wide64.o
BASEFLAGS := $(ARCHFLAGS) -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx -msoft-float
CFLAGS := $(BASEFLAGS) -std=c11 -O2 -g1 -Wall -Wextra -Werror -Iinclude -MMD -MP -ffunction-sections -fdata-sections
ASFLAGS := $(BASEFLAGS) -g
KCS := $(wildcard kernel/*.c) common/string.c common/acpi.c common/pci_decode.c
KAS := $(wildcard arch/$(ARCHDIR)/*.S)
KOBJS := $(patsubst %.c,$(BUILD)/%.o,$(KCS)) $(patsubst %.S,$(BUILD)/%.o,$(KAS))
APPS := loom pulse spin fault probe folio relay vector
UELFS := $(addprefix $(BUILD)/apps/,$(addsuffix .elf,$(APPS)))
UCOMMON := $(BUILD)/user/runtime.o $(BUILD)/user/$(USTART).o $(BUILD)/common/string.o $(UEXTRA)
.DELETE_ON_ERROR:
.PHONY: all clean run window test test-host iso iso-uefi esp disk check FORCE
all: $(BUILD)/boot.elf $(BUILD)/BOOTX64.EFI
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/arch/$(ARCHDIR)/boot.o: arch/cpu_boot.inc
$(BUILD)/arch/x86_64/uefi.o: arch/x86_64/uefi.c $(wildcard include/nv/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -mcmodel=large -fshort-wchar -c $< -o $@
$(BUILD)/arch/x86_64/uefi-string.o: common/string.c include/nv/string.h include/nv/types.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -mcmodel=large -c $< -o $@
$(BUILD)/uefi.elf: $(BUILD)/arch/x86_64/uefi.o $(BUILD)/arch/x86_64/uefi-string.o arch/x86_64/uefi.ld
	$(LD) -m $(MACHINE) --emit-relocs -z max-page-size=4096 -T arch/x86_64/uefi.ld -Map $(BUILD)/uefi.map -o $@ $(filter %.o,$^)
$(BUILD)/BOOTX64.EFI: $(BUILD)/uefi.elf
	$(PYTHON) scripts/mkuefi.py $< $@
$(BUILD)/esp.img: $(BUILD)/BOOTX64.EFI $(BUILD)/nuvora.elf scripts/mkesp.py FORCE
	$(PYTHON) scripts/mkesp.py $@
esp: $(BUILD)/esp.img
$(BUILD)/apps/folio.elf: $(BUILD)/user/folio.o $(BUILD)/user/document.o $(UCOMMON) $(ULINK)
$(BUILD)/apps/%.elf: $(BUILD)/user/%.o $(UCOMMON) $(ULINK)
	@mkdir -p $(dir $@)
	$(LD) -m $(MACHINE) --gc-sections -z max-page-size=4096 -T $(ULINK) -o $@ $(filter %.o,$^)
$(BUILD)/init.nvar: $(UELFS) scripts/mkarchive.py
	$(PYTHON) scripts/mkarchive.py $@ $(UELFS)
$(BUILD)/archive.S: $(BUILD)/init.nvar
	$(PYTHON) scripts/mkarchive.py --assembly $@
$(BUILD)/archive.o: $(BUILD)/archive.S
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/nuvora.elf: $(KOBJS) $(BUILD)/archive.o arch/$(ARCHDIR)/linker.ld
	$(LD) -m $(MACHINE) --gc-sections -z max-page-size=4096 -T arch/$(ARCHDIR)/linker.ld -Map $(BUILD)/nuvora.map -o $@ $(filter %.o,$^)
	$(PYTHON) scripts/check_image.py $@
$(BUILD)/boot.elf: $(BUILD)/nuvora.elf
	$(OBJCOPY) -O elf32-i386 $< $@
	$(PYTHON) scripts/check_image.py $@
disk:
	$(PYTHON) scripts/mkdisk.py $(BUILD)/nuvora-store.img --if-missing
run: all disk
	$(PYTHON) scripts/run.py
window: all disk
	$(PYTHON) scripts/run.py --window
test: all
	$(PYTHON) scripts/test.py
test-host: all
	$(PYTHON) scripts/test_regressions.py
iso: all
	$(PYTHON) scripts/mkiso.py
iso-uefi: all $(BUILD)/esp.img
	$(PYTHON) scripts/mkiso.py --uefi
check: all
	$(PYTHON) scripts/check_image.py $(BUILD)/nuvora.elf
clean:
	rm -rf $(BUILD)
-include $(KOBJS:.o=.d) $(wildcard $(BUILD)/user/*.d) $(BUILD)/arch/x86_64/uefi.d
.SECONDARY: $(UCOMMON) $(patsubst %,$(BUILD)/user/%.o,$(APPS))
endif
