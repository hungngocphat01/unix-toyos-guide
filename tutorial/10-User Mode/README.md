# 10 — User Mode

**Goal:** run code in **ring 3** in its own address space, and come back
into the kernel when it's interrupted or misbehaves.

**Milestone:**

```
entering user mode at 00400000...
trap 13 (general protection), err=0
  eip=0040000d cs=001b eflags=00010246
  eax=00001234 ebx=00000000 ecx=00000000 edx=00000000
  esi=00000000 edi=00000000 ebp=00000000
  ds=0023 es=0023 fs=0023 gs=0023
  user esp=c0000000 ss=0023
user program faulted after 14 interrupts in user mode
```

The program ran at `0x00400000` in ring 3 (`cs=001b`), was interrupted 14
times by the timer and resumed each time, then executed `cli`, which ring 3
isn't allowed to do.

**Reference code:** [`code/`](code/). New: [`user_demo.asm`](code/kernel/user_demo.asm).
Changed: TSS in [`gdt.c`](code/kernel/arch/i386/gdt.c);
`enter_usermode` in [`isr.asm`](code/kernel/arch/i386/isr.asm);
`from_user()` in [`trap.c`](code/kernel/arch/i386/trap.c);
`vmm_new_pd()` in [`vmm.c`](code/kernel/mm/vmm.c);
[`kmain.c`](code/kernel/kmain.c).

---

## 1. The mental model

### What "user mode" is, mechanically

Three facts, each one you've already met:

1. **CPL = the low 2 bits of CS.** Running with `CS = 0x1B` (GDT[3], DPL 3)
   *is* user mode (lesson 3).
2. **Ring 3 can't** execute privileged instructions (`cli`, `hlt`,
   `mov cr3`, `lgdt`, `in`/`out`...) → **#GP**. And it can't touch pages
   without the **U** bit → **#PF** (lesson 7).
3. **The only way back into ring 0 is an interrupt** (hardware IRQ,
   exception, or `int 0x80` in the next lesson). The IDT gate's selector
   is `0x08`, so the CPU loads a ring-0 CS as part of delivering it.

So the kernel never "calls" user code, and user code never "calls" the
kernel. They take turns, and every switch goes through the trap frame.

### The problem: whose stack?

When an interrupt arrives in ring 3, ESP points into the **user's** stack.
The kernel can't push its trap frame there: the user could have set ESP to
garbage, or to a kernel address, or another thread could be reading it. So
**on a ring 3 → ring 0 transition the CPU switches stacks**, and it gets the
new stack from the **TSS** (Task State Segment): fields `ss0:esp0`.

Then it pushes the **old** SS and ESP onto the new stack, so `iret` can
restore them. That's where the two extra fields of the trap frame come from:

```
 interrupt from ring 0 (lesson 4)        interrupt from ring 3 (now)
 ────────────────────────────────        ──────────────────────────────────────
 stays on the current kernel stack       CPU loads SS:ESP = tss.ss0:tss.esp0
                                         then pushes, on the KERNEL stack:
                                             user SS     ← trapframe.user_ss
                                             user ESP    ← trapframe.user_esp
 pushes EFLAGS, CS, EIP                  pushes EFLAGS, CS (=0x1B), EIP
 (stub + trap_common as before)          (stub + trap_common as before)
```

`iret` does the reverse: it pops EIP, CS, EFLAGS, **sees that CS has RPL 3**,
so it also pops ESP and SS and switches back to the user stack.

Concretely, with `esp0 = 0xC0150000` and the program interrupted at
`0x00400008`:

```
 0xC014FFFC  user SS   = 0x23         ← tss.esp0 was 0xC0150000; first push lands below it
 0xC014FFF8  user ESP  = 0xBFFFFFxx
 0xC014FFF4  EFLAGS    = 0x00000246
 0xC014FFF0  CS        = 0x1B
 0xC014FFEC  EIP       = 0x00400008
 0xC014FFE8  err, then trapno, then ds es fs gs, then pushad...  (same as lesson 4)
```

**The TSS has one job here: holding `esp0`.** Ignore its other 100 bytes:
they're for hardware task switching, which no modern OS uses. In lesson 23,
each process gets its own kernel stack, and switching processes includes
`tss.esp0 = top of the new process's kernel stack`.

### Getting into user mode the first time: fake a return

There's no instruction for "jump to ring 3". But `iret` happily goes to
ring 3 if the frame it pops says so. So we **build a trap frame by hand**
as if user code had been interrupted at its first instruction, and run
the normal interrupt exit path on it:

```c
struct trapframe tf;
memset(&tf, 0, sizeof(tf));
tf.cs       = SEG_UCODE;                        /* 0x1B: ring 3            */
tf.ds = tf.es = tf.fs = tf.gs = SEG_UDATA;      /* 0x23                    */
tf.user_ss  = SEG_UDATA;
tf.user_esp = USER_STACK;                       /* top of the user stack   */
tf.eip      = USER_CODE;                        /* first instruction       */
tf.eflags   = EFLAGS_IF;                        /* interrupts on in ring 3 */
enter_usermode(&tf);
```

```nasm
enter_usermode:
    mov esp, [esp + 4]      ; ESP = &tf
    jmp trapret             ; popad, pop gs/fs/es/ds, skip trapno/err, iretd
```

Reusing `trapret` instead of writing a separate "enter user mode" routine
means there's exactly one way to return to user mode in this kernel.
Lesson 12 does exactly this for every new process.

Note `EFLAGS_IF`: the kernel never ran `sti` in this lesson. Interrupts
turn on *in user mode* because the frame says so. (And they turn off
again on entry to the kernel because our gates are interrupt gates.)

### The address space

`vmm_new_pd()` makes a page directory whose **entries 768–1023 are copied
from `kernel_pd`**. They point at the *same* page tables, so the kernel is
mapped identically in every address space, and entries 0–767 start empty:

```
 kernel_pd                       user pd (new frame)
 [0..767]   empty                [0..767]   user's own: [1] → page table → code frame
 [768..831] ─► kernel PTs ◄───── [768..831] same pointers
```

This works because `vmm_init()` created **all** the kernel's page tables
up front (the whole direct map). If the kernel later added a page table
above `KERNBASE`, old user directories wouldn't see it. We never do.

Then: the code page at `0x00400000` with `PTE_U` (readable, not writable),
the stack page just below `0xC0000000` with `PTE_U | PTE_W`. The kernel's own
pages have no `PTE_U`, so the user can't read or write them even though
they're in its page directory.

Loading the new directory into CR3 *while running kernel code* is fine:
the kernel half is identical in both.

---

## 2. Modules

| File | Change | Exposes |
|---|---|---|
| [`arch/i386/gdt.c`](code/kernel/arch/i386/gdt.c) | `struct tss`, GDT[5] = TSS descriptor (access `0x89`, byte granularity), `ltr 0x28` | `tss_set_kernel_stack(esp0)`, `SEG_TSS` |
| [`arch/i386/isr.asm`](code/kernel/arch/i386/isr.asm) | `enter_usermode` | `enter_usermode(tf)` |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | `from_user(tf)`; print user ESP/SS; a fault from ring 3 doesn't panic | |
| [`mm/vmm.c`](code/kernel/mm/vmm.c) | new address spaces | `vmm_new_pd()` |
| [`user_demo.asm`](code/kernel/user_demo.asm) | a 16-byte user program, embedded as data | `user_demo_start/end` |
| [`kmain.c`](code/kernel/kmain.c) | `run_user_demo()`: steps 1–4 above | |

The TSS descriptor is a system descriptor: `S = 0` in the access byte,
type `9` = "32-bit TSS, not busy". `iomap_base = sizeof(tss)` puts the I/O
permission bitmap past the end of the segment, which means "no ports
allowed" for ring 3.

## 3. Test

- The milestone.
- Replace `cli` with `mov dword [0xC0100000], 1` (write to the kernel): page
  fault, `protection violation, write, user mode`, `cs=001b`.
- Replace it with `jmp $` (spin forever): the kernel still gets timer
  interrupts and the keyboard still echoes. User code can't hog the
  machine because it can't turn interrupts off.
- Set `tss.esp0` to 0: the first timer tick in ring 3 → the CPU can't push
  the frame → double/triple fault. That's what a missing TSS looks like.

## Pitfalls

- **Forgetting `ltr`**, or a wrong TSS limit/type: the first interrupt from
  ring 3 → #TS or triple fault.
- **`esp0` pointing at the bottom of the stack page instead of the top**:
  stacks grow down.
- **Code page mapped without `PTE_U`**: the very first instruction faults
  (`page fault, protection violation, read, user mode` at `0x00400000`).
- **Leaving DS/ES as kernel selectors** in the frame: `iret` to ring 3
  zeroes data segment registers whose DPL < 3, and the first memory access
  faults.
- **Reading `tf->user_esp` for kernel-mode traps**: only valid if
  `from_user(tf)`.

## References

- Intel SDM Vol. 3A §6.12.1 (stack switch on privilege change, Figure 6-4),
  §7.2.1 (TSS layout), §5.8 (privilege checks).
- OSDev: "Task State Segment", "Getting to Ring 3"
- xv6: `switchuvm()` in `vm.c` (sets `esp0`), `userinit()` in `proc.c` (fake
  trap frame), `initcode.S` (the embedded first program).
