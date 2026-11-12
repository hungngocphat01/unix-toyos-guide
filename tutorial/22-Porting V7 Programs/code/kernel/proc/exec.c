/* exec.c -- replace the current process's program with a new one.
 *
 *   1. find the program file and check its ELF header
 *   2. build a NEW address space: load each PT_LOAD segment, add a stack,
 *      and copy argv/envp onto the stack
 *   3. only then (nothing can fail anymore) swap it in and free the old one
 *   4. point the trap frame at the program's entry point
 *
 * If anything fails before step 3, the old program is untouched and exec()
 * returns an error to it, as Unix requires.
 */
#include "proc/proc.h"
#include "proc/elf.h"
#include "fs/fs.h"
#include "mm/uvm.h"
#include "mm/memlayout.h"
#include "mm/kheap.h"
#include "arch/i386/cpu.h"
#include "arch/i386/gdt.h"
#include "abi/errno.h"
#include "lib/string.h"

/* The program's bytes come from a file. */
struct image {
    struct inode *ip;
};

static int read_image(struct image *img, uint32_t off, void *dst, uint32_t n)
{
    return readi(img->ip, dst, off, n) == (int)n ? 0 : -1;
}

/* Load every PT_LOAD segment into pd. Sets *entry and *end (the highest
 * address used, where the heap will start). */
static int load_elf(pde_t *pd, struct image *img, uint32_t *entry, uint32_t *end)
{
    struct elf_header eh;
    if (read_image(img, 0, &eh, sizeof(eh)) < 0 || eh.magic != ELF_MAGIC ||
        eh.class != ELF_CLASS32 || eh.type != ELF_EXEC || eh.machine != ELF_MACH_386)
        return -ENOEXEC;

    char *buf = kmalloc(1024);
    if (!buf)
        return -ENOMEM;

    *end = 0;
    for (int i = 0; i < eh.phnum; i++) {
        struct elf_phdr ph;
        if (read_image(img, eh.phoff + i * sizeof(ph), &ph, sizeof(ph)) < 0)
            goto bad;
        if (ph.type != PT_LOAD)
            continue;

        uint32_t seg_end = ph.vaddr + ph.memsz;
        if (ph.vaddr < USER_BASE || seg_end < ph.vaddr || ph.filesz > ph.memsz ||
            seg_end > USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE)
            goto bad;

        uint32_t flags = PTE_U | ((ph.flags & PF_W) ? PTE_W : 0);
        if (uvm_alloc(pd, ph.vaddr, seg_end, flags) < 0) {
            kfree(buf);
            return -ENOMEM;
        }

        /* Copy the file part in chunks. The rest (.bss) is already zero
         * because uvm_alloc gives zeroed frames. */
        for (uint32_t done = 0; done < ph.filesz; ) {
            uint32_t n = ph.filesz - done;
            if (n > 1024)
                n = 1024;
            if (read_image(img, ph.offset + done, buf, n) < 0 ||
                uvm_copy_to(pd, ph.vaddr + done, buf, n) < 0)
                goto bad;
            done += n;
        }
        if (seg_end > *end)
            *end = seg_end;
    }
    kfree(buf);
    *entry = eh.entry;
    return 0;

bad:
    kfree(buf);
    return -ENOEXEC;
}

static int count(char *const v[])
{
    int n = 0;
    while (v[n])
        n++;
    return n;
}

/* Build the initial stack, exactly like V7:
 *
 *   high addresses   ...strings ("hello\0", "arg1\0", "HOME=/\0")
 *                    NULL                       <- end of envp
 *                    envp[0..envc-1]
 *                    NULL                       <- end of argv
 *                    argv[0..argc-1]
 *   *sp ->           argc
 *
 * Returns the new stack pointer, or 0 if it doesn't fit. */
static uint32_t setup_stack(pde_t *pd, char *const argv[], char *const envp[])
{
    int argc = count(argv), envc = count(envp);
    uint32_t stack_bottom = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;

    uint32_t strings = 0;
    for (int i = 0; i < argc; i++)
        strings += strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++)
        strings += strlen(envp[i]) + 1;

    uint32_t str_addr = (USER_STACK_TOP - strings) & ~3;        /* 4-byte align */
    uint32_t sp = str_addr - 4 * (1 + argc + 1 + envc + 1);
    if (sp < stack_bottom + PAGE_SIZE)                          /* leave room */
        return 0;

    uint32_t word = argc;
    uint32_t at = sp;
    uvm_copy_to(pd, at, &word, 4);
    at += 4;

    char *const *lists[2] = { argv, envp };
    int counts[2] = { argc, envc };
    for (int l = 0; l < 2; l++) {
        for (int i = 0; i < counts[l]; i++) {
            uint32_t len = strlen(lists[l][i]) + 1;
            uvm_copy_to(pd, str_addr, lists[l][i], len);        /* the string  */
            uvm_copy_to(pd, at, &str_addr, 4);                  /* its pointer */
            str_addr += len;
            at += 4;
        }
        word = 0;
        uvm_copy_to(pd, at, &word, 4);                          /* NULL */
        at += 4;
    }
    return sp;
}

int exec(const char *path, char *const argv[], char *const envp[])
{
    struct image img;
    int err;
    img.ip = namei(path, &err);
    if (!img.ip)
        return err;
    if ((img.ip->d.mode & S_IFMT) != S_IFREG || iaccess(img.ip, S_IEXEC) < 0) {
        iput(img.ip);
        return -EACCES;             /* not a regular file we may execute */
    }
    uint16_t mode = img.ip->d.mode;
    uint16_t owner = img.ip->d.uid, group = img.ip->d.gid;

    pde_t *pd = vmm_new_pd();
    if (!pd) {
        iput(img.ip);
        return -ENOMEM;
    }

    uint32_t entry = 0, end = 0, sp = 0;
    err = load_elf(pd, &img, &entry, &end);
    if (err == 0) {
        uint32_t stack_bottom = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
        if (uvm_alloc(pd, stack_bottom, USER_STACK_TOP, PTE_U | PTE_W) < 0)
            err = -ENOMEM;
        else if ((sp = setup_stack(pd, argv, envp)) == 0)
            err = -E2BIG;
    }
    iput(img.ip);
    if (err < 0) {
        uvm_free(pd);
        return err;
    }

    /* Point of no return: switch to the new address space. */
    struct proc *p = current;
    pde_t *old = p->pd;
    p->pd = pd;
    p->brk_start = p->brk = end;
    const char *name = path;            /* "/bin/ls" -> "ls" */
    for (const char *c = path; *c; c++)
        if (*c == '/')
            name = c + 1;
    strncpy(p->name, name, sizeof(p->name) - 1);

    /* A setuid (setgid) program runs with its owner's (group's) rights:
     * that's how passwd can write /etc/passwd, or mkdir call mknod. */
    if (mode & S_ISUID)
        p->uid = owner;
    if (mode & S_ISGID)
        p->gid = group;

    write_cr3(V2P(pd));
    uvm_free(old);

    /* A fresh trap frame: start at the entry point, all registers zero. */
    struct trapframe *tf = proc_trapframe();
    memset(tf, 0, sizeof(*tf));
    tf->cs = SEG_UCODE;
    tf->ds = tf->es = tf->fs = tf->gs = SEG_UDATA;
    tf->user_ss = SEG_UDATA;
    tf->user_esp = sp;
    tf->eip = entry;
    tf->eflags = EFLAGS_IF;
    return 0;
}
