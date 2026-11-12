# 3 — The GDT

**Goal:** replace the loader's segment table with your own, including the
two **user-mode** segments you'll need in lesson 10.

**Milestone:**

```
loader's GDT: base=000cb2b8 limit=39 cs=8
our GDT     : base=00102020 limit=39 cs=8
```

The base moved from the loader's memory into your kernel's `.bss`
(`i686-elf-nm build/kernel.elf | grep gdt` shows `00102020 b gdt`).

**Reference code:** [`code/`](code/). New: [`gdt.h`](code/kernel/arch/i386/gdt.h),
[`gdt.c`](code/kernel/arch/i386/gdt.c), [`gdt_load.asm`](code/kernel/arch/i386/gdt_load.asm);
`kmain.c` calls `gdt_init()`.

---

## Concepts

### Why bother with segmentation at all?

On i386 you can't turn segmentation off. Every memory access goes through
a segment: `linear = segment.base + offset`. We make it a no-op with the
**flat model**: every segment has base 0 and limit 4 GiB, so
`linear = offset`. Paging (lesson 7) does the real memory work.

So why have our own GDT? Because segments carry the one thing we really
need from them: **the privilege level.**

```
 CS selector = 0x08 → GDT[1], DPL 0 → CPU is in ring 0 (kernel mode)
 CS selector = 0x1B → GDT[3], DPL 3 → CPU is in ring 3 (user mode)
```

"User mode" *is* "CS points at a DPL-3 code segment". The current privilege
level (CPL) is the low 2 bits of CS. Later:

- an interrupt arriving in ring 3 makes the CPU load a ring-0 CS (from the
  IDT) and switch stacks (lessons 4, 10);
- `iret` back to a frame whose CS is `0x1B` drops to ring 3 (lesson 10);
- paging checks the CPL: ring 3 can't touch pages without the U/S bit (lesson 7).

Also, the loader's GDT lives in memory you don't own (at `0xcb2b8`, in
QEMU's BIOS area). Once you start reusing memory, it could be overwritten,
and the next segment load would crash.

### The table

| Index | Selector | Segment | Access byte |
|---|---|---|---|
| 0 | `0x00` | null (required, never used) | — |
| 1 | `0x08` | kernel code | `0x9A` |
| 2 | `0x10` | kernel data (also stack) | `0x92` |
| 3 | `0x1B` | user code (`0x18 \| RPL 3`) | `0xFA` |
| 4 | `0x23` | user data (`0x20 \| RPL 3`) | `0xF2` |
| 5 | `0x28` | TSS (added in lesson 10) | `0x89` |

A **selector** is `index * 8 | TI | RPL`: index into the table, TI=0 for
GDT, RPL = the privilege you claim. User selectors carry RPL 3, hence
`0x1B`/`0x23` (you'll see those numbers in every trap frame from user mode).

Decoding an access byte, e.g. `0xFA = 1111 1010`:

```
 P=1  DPL=11 (ring 3)  S=1 (code/data)  type=1010 (code, readable)
```

Each entry also has a 20-bit limit and a flags nibble. Flags `0xC` = G
(limit is in 4 KiB units: `0xFFFFF` × 4 KiB = 4 GiB) + D (32-bit). The
base and limit are split across the 8 bytes for historical (80286)
reasons; [`gdt.c`](code/kernel/arch/i386/gdt.c) hides that in one function:

```c
static void set_entry(int i, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    gdt[i].limit_low        = limit & 0xFFFF;
    gdt[i].base_low         = base & 0xFFFF;
    gdt[i].base_mid         = (base >> 16) & 0xFF;
    gdt[i].access           = access;
    gdt[i].limit_high_flags = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_high        = (base >> 24) & 0xFF;
}
```

### Loading it: the part you need assembly for

`lgdt` only tells the CPU where the table is. Each segment register holds a
**cached copy** of its descriptor from when it was last loaded, so nothing
changes until you reload them. Data registers take a plain `mov`. CS can't
be `mov`ed; a **far jump** reloads it:

```nasm
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10                ; kernel data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs         ; far jump: CS = 0x08
.reload_cs:
    ret
```

(Reloading SS in the middle of a function is fine: same base 0, so ESP still
points at the same stack.)

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`arch/i386/gdt.h`](code/kernel/arch/i386/gdt.h) | selector constants | `SEG_KCODE SEG_KDATA SEG_UCODE SEG_UDATA`, `gdt_init()` |
| [`arch/i386/gdt.c`](code/kernel/arch/i386/gdt.c) | build the table | `gdt_init()` |
| [`arch/i386/gdt_load.asm`](code/kernel/arch/i386/gdt_load.asm) | `lgdt` + reload segments | `gdt_load()` (only used by gdt.c) |

The selector constants are the module's real interface: the IDT (next
lesson) and user mode (lesson 10) use them.

## Test

The milestone output. To convince yourself it matters, break it:

- Set kernel data's access byte to `0x00` (not present). Reboot loop.
  Enable the QEMU log (Makefile comment) and find `v=0d` or `v=0b`: the CPU
  refused to load DS.
- Set the GDT limit to `8` (only the null entry). Same thing for CS.

## Pitfalls

- **Forgetting `packed`** on either struct: `gdt_ptr` becomes 8 bytes, with
  the base at the wrong offset.
- **Limit = size, not size − 1.**
- **Not reloading CS.** Everything seems fine until the next interrupt
  returns with a stale CS and the machine resets.
- Putting the GDT on the stack (a local variable): the CPU keeps reading it
  after the function returns.

## References

- Intel SDM Vol. 3A §3.4 (selectors), §3.4.5 (descriptor format), §5.5 (privilege levels)
- OSDev: "GDT", "GDT Tutorial", "Segmentation"
- xv6: `seginit()` in `vm.c`, `mmu.h`
