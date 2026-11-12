# toyos top-level Makefile
#
#   make            build build/kernel.elf
#   make run        boot in QEMU (VGA window + serial on this terminal)
#   make run-nox    boot in QEMU with no window (serial only; quit: Ctrl-A then X)
#   make run-test   like run-nox but no monitor: for scripted tests (kill it or use `timeout`)
#   make debug      boot paused, waiting for GDB on :1234
#   make gdb        attach i386-elf-gdb to a `make debug` session
#   make clean

CC      := i686-elf-gcc
AS      := nasm
GDB     := i386-elf-gdb
QEMU    := qemu-system-i386

BUILD   := build
KERNEL  := $(BUILD)/kernel.elf
LDSCRIPT:= kernel/arch/i386/linker.ld

CFLAGS  := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic \
           -fno-tree-loop-distribute-patterns \
           -O2 -g -Wall -Wextra -Ikernel -I.
ASFLAGS := -f elf32 -g -F dwarf
LDFLAGS := -T $(LDSCRIPT) -nostdlib -ffreestanding -O2 -g

# Every .c and .asm under kernel/ is part of the kernel.
C_SRCS   := $(shell find kernel -name '*.c')
ASM_SRCS := $(shell find kernel -name '*.asm')
OBJS     := $(C_SRCS:%.c=$(BUILD)/%.o) $(ASM_SRCS:%.asm=$(BUILD)/%.asm.o)

QEMUFLAGS := -m 128M -serial stdio -no-reboot -no-shutdown
# Uncomment to log every interrupt/exception to build/qemu.log (very useful in lessons 3-8):
# QEMUFLAGS += -d int,cpu_reset -D $(BUILD)/qemu.log

.PHONY: all run run-nox run-test debug gdb clean

all: $(KERNEL)

$(KERNEL): $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

run: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) $(QEMUEXTRA) -kernel $(KERNEL)

run-nox: $(KERNEL)
	$(QEMU) $(filter-out -serial stdio,$(QEMUFLAGS)) -nographic $(QEMUEXTRA) -kernel $(KERNEL)

run-test: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) -display none $(QEMUEXTRA) -kernel $(KERNEL)

debug: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) $(QEMUEXTRA) -kernel $(KERNEL) -s -S

gdb: $(KERNEL)
	$(GDB) $(KERNEL) -ex 'target remote localhost:1234'

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
