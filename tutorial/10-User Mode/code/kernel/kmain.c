/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/cpu.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/pic.h"
#include "arch/i386/pit.h"
#include "arch/i386/trap.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "dev/kbd.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "lib/string.h"

#define USER_CODE   0x00400000      /* where the user program is mapped    */
#define USER_STACK  0xC0000000      /* top of the user stack (grows down)  */

extern char user_demo_start[], user_demo_end[];     /* user_demo.asm */

static void run_user_demo(void)
{
    /* 1. A new address space: kernel above KERNBASE, nothing below. */
    pde_t *pd = vmm_new_pd();

    /* 2. One page of code (user-readable, not writable) ... */
    uint32_t code = pmm_alloc();
    memcpy(P2V(code), user_demo_start, user_demo_end - user_demo_start);
    vmm_map(pd, USER_CODE, code, PTE_U);

    /* ... and one page of stack (user-readable and writable). */
    vmm_map(pd, USER_STACK - PAGE_SIZE, pmm_alloc(), PTE_U | PTE_W);

    /* 3. The stack the CPU switches to when an interrupt arrives in ring 3. */
    uint32_t kstack = (uint32_t)P2V(pmm_alloc());
    tss_set_kernel_stack(kstack + PAGE_SIZE);

    /* 4. A trap frame that "returns" to the start of the program in ring 3. */
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    tf.cs = SEG_UCODE;
    tf.ds = tf.es = tf.fs = tf.gs = SEG_UDATA;
    tf.user_ss = SEG_UDATA;
    tf.user_esp = USER_STACK;
    tf.eip = USER_CODE;
    tf.eflags = EFLAGS_IF;              /* interrupts on in user mode */

    write_cr3(V2P(pd));
    kprintf("entering user mode at %08lx...\n", tf.eip);
    enter_usermode(&tf);
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    console_init();
    kprintf("toyos booting...\n");
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    gdt_init();
    idt_init();
    pic_init();
    pit_init();
    kbd_init();
    pmm_init(P2V(mbi_phys));
    vmm_init();

    run_user_demo();
}
