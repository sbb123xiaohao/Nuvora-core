SHELL := /bin/sh
CROSS ?=
CC := $(CROSS)gcc
LD := $(CROSS)ld
PYTHON ?= python3
ARCH ?= x86_64
ifeq ($(filter $(ARCH),i686 x86_64),)
$(error ARCH must be i686 or x86_64)
endif
BUILD := build/$(ARCH)
export NV_ARCH := $(ARCH)
OBJCOPY := $(CROSS)objcopy
ifeq ($(ARCH),x86_64)
ARCHDIR := x86_64
MACHINE := elf_x86_64
ARCHFLAGS := -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only
ULINK := user/linker64.ld
USTART := start64
UEXTRA := $(BUILD)/user/wide64.o
else
ARCHDIR := i386
MACHINE := elf_i386
ARCHFLAGS := -m32 -march=i686 -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2
ULINK := user/linker.ld
USTART := start
endif
BASEFLAGS := $(ARCHFLAGS) -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx -msoft-float
CFLAGS := $(BASEFLAGS) -std=c11 -O2 -g1 -Wall -Wextra -Werror -Iinclude -MMD -MP -ffunction-sections -fdata-sections
ASFLAGS := $(BASEFLAGS) -g
KCS := $(wildcard kernel/*.c) common/string.c
KAS := $(wildcard arch/$(ARCHDIR)/*.S)
KOBJS := $(patsubst %.c,$(BUILD)/%.o,$(KCS)) $(patsubst %.S,$(BUILD)/%.o,$(KAS))
APPS := loom pulse spin fault probe folio relay
UELFS := $(addprefix $(BUILD)/apps/,$(addsuffix .elf,$(APPS)))
UCOMMON := $(BUILD)/user/runtime.o $(BUILD)/user/$(USTART).o $(BUILD)/common/string.o $(UEXTRA)
.PHONY: all clean run window test iso disk check
all: $(BUILD)/boot.elf
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
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
ifeq ($(ARCH),x86_64)
	$(OBJCOPY) -O elf32-i386 $< $@
else
	cp $< $@
endif
	$(PYTHON) scripts/check_image.py $@
disk:
	$(PYTHON) scripts/mkdisk.py $(BUILD)/nuvora-store.img --if-missing
run: all disk
	$(PYTHON) scripts/run.py
window: all disk
	$(PYTHON) scripts/run.py --window
test: all
	$(PYTHON) scripts/test.py
iso: all
	$(PYTHON) scripts/mkiso.py
check: all
	$(PYTHON) scripts/check_image.py $(BUILD)/nuvora.elf
clean:
	rm -rf $(BUILD)
-include $(KOBJS:.o=.d) $(wildcard $(BUILD)/user/*.d)
.SECONDARY: $(UCOMMON) $(patsubst %,$(BUILD)/user/%.o,$(APPS))
