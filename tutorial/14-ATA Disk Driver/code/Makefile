# toyos top-level Makefile
#
#   make            build the kernel and the user programs
#   make run        boot in QEMU (VGA window + serial on this terminal)
#   make run-nox    boot in QEMU with no window (serial only; quit: Ctrl-A then X)
#   make run-test   like run-nox but no monitor: for scripted tests (kill it or use `timeout`)
#   make debug      boot paused, waiting for GDB on :1234
#   make gdb        attach i386-elf-gdb to a `make debug` session
#   make clean

CC      := i686-elf-gcc
AR      := i686-elf-ar
AS      := nasm
GDB     := i386-elf-gdb
QEMU    := qemu-system-i386

BUILD   := build

.DEFAULT_GOAL := all          # plain `make` = `make all`

# ---------------------------------------------------------------- kernel

KERNEL   := $(BUILD)/kernel.elf
LDSCRIPT := kernel/arch/i386/linker.ld

CFLAGS  := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic \
           -fno-tree-loop-distribute-patterns \
           -O2 -g -Wall -Wextra -Ikernel -I.
ASFLAGS := -f elf32 -g -F dwarf
LDFLAGS := -T $(LDSCRIPT) -nostdlib -ffreestanding -O2 -g

# Every .c and .asm under kernel/ is part of the kernel.
C_SRCS   := $(shell find kernel -name '*.c')
ASM_SRCS := $(shell find kernel -name '*.asm')
OBJS     := $(C_SRCS:%.c=$(BUILD)/%.o) $(ASM_SRCS:%.asm=$(BUILD)/%.asm.o)

$(KERNEL): $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# ---------------------------------------------------------------- user space

# User programs see only our libc headers (plus the compiler's own
# stdarg.h/stddef.h/stdint.h), never the host's.
USER_CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic \
               -fno-tree-loop-distribute-patterns -O2 -g -Wall -Wextra \
               -nostdinc -isystem $(shell $(CC) -print-file-name=include) \
               -Iuser/libc/include -I.
USER_LDFLAGS := -T user/user.ld -nostdlib -static

LIBC      := $(BUILD)/user/libc.a
CRT0      := $(BUILD)/user/libc/crt0.asm.o
LIBC_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard user/libc/*.c))

# Each user/bin/NAME.c becomes the program build/user/bin/NAME
USER_PROGS := $(patsubst user/bin/%.c,$(BUILD)/user/bin/%,$(wildcard user/bin/*.c))

$(BUILD)/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -MMD -MP -c $< -o $@

$(LIBC): $(LIBC_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/user/bin/%: $(BUILD)/user/bin/%.o $(CRT0) $(LIBC) user/user.ld
	$(CC) $(USER_LDFLAGS) -o $@ $(CRT0) $< $(LIBC) -lgcc

# ---------------------------------------------------------------- running

# Boot modules: QEMU loads each file next to the kernel (lesson 12).
comma := ,
empty :=
space := $(empty) $(empty)
INITRD := $(subst $(space),$(comma),$(USER_PROGS))

# The hard disk (lesson 14). Created once; your changes survive reboots.
DISK := $(BUILD)/disk.img

$(DISK):
	@mkdir -p $(BUILD)
	dd if=/dev/zero of=$@ bs=1024 count=8192 2>/dev/null
	printf 'toyos test disk, sector 0' | dd of=$@ conv=notrunc 2>/dev/null

QEMUFLAGS := -m 128M -serial stdio -no-reboot -no-shutdown -initrd "$(INITRD)" \
             -drive file=$(DISK),format=raw,if=ide,index=0
# Uncomment to log every interrupt/exception to build/qemu.log:
# QEMUFLAGS += -d int,cpu_reset -D $(BUILD)/qemu.log

.PHONY: all run run-nox run-test debug gdb clean

all: $(KERNEL) $(USER_PROGS) $(DISK)

run: all
	$(QEMU) $(QEMUFLAGS) $(QEMUEXTRA) -kernel $(KERNEL)

run-nox: all
	$(QEMU) $(filter-out -serial stdio,$(QEMUFLAGS)) -nographic $(QEMUEXTRA) -kernel $(KERNEL)

run-test: all
	$(QEMU) $(QEMUFLAGS) -display none $(QEMUEXTRA) -kernel $(KERNEL)

debug: all
	$(QEMU) $(QEMUFLAGS) $(QEMUEXTRA) -kernel $(KERNEL) -s -S

gdb: $(KERNEL)
	$(GDB) $(KERNEL) -ex 'target remote localhost:1234'

clean:
	rm -rf $(BUILD)

.SECONDARY:                     # keep the intermediate .o files

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
