/* sys_proc.c -- process-related system calls. */
#include <stddef.h>
#include "sys/syscall.h"
#include "proc/proc.h"
#include "mm/uvm.h"
#include "mm/pmm.h"
#include "mm/memlayout.h"
#include "abi/errno.h"

/* exit(status) */
int32_t sys_exit(struct trapframe *tf)
{
    proc_exit((tf->ebx & 0xFF) << 8);
}

/* getpid() */
int32_t sys_getpid(struct trapframe *tf)
{
    (void)tf;
    return current->pid;
}

/* brk(addr): set the end of the heap. Returns 0 or -1 (V7 "break"). */
int32_t sys_brk(struct trapframe *tf)
{
    struct proc *p = current;
    uint32_t new_brk = tf->ebx;

    if (new_brk < p->brk_start ||
        new_brk > USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE)
        return -ENOMEM;

    if (new_brk > p->brk) {
        if (uvm_alloc(p->pd, p->brk, new_brk, PTE_U | PTE_W) < 0)
            return -ENOMEM;
    } else {
        uvm_dealloc(p->pd, PAGE_ROUND_UP(new_brk), PAGE_ROUND_UP(p->brk));
    }
    p->brk = new_brk;
    return 0;
}

/* ---- exec ---- */

#define MAXARG   64             /* max number of argv (and of envp) strings */
#define PATH_MAX 256

/* Everything exec needs, copied out of user memory before the old address
 * space is destroyed. Fits in one page. */
struct exec_args {
    char  path[PATH_MAX];
    char *argv[MAXARG + 1];
    char *envp[MAXARG + 1];
    char  pool[];               /* the strings themselves, up to the page end */
};

/* Copy a NULL-terminated user array of user strings into karr, storing the
 * characters in [*pool, pool_end). */
static int fetch_string_array(char *const *uarr, char **karr,
                              char **pool, char *pool_end)
{
    int i;
    for (i = 0; ; i++) {
        if (!user_ok(&uarr[i], sizeof(char *), 0))
            return -EFAULT;
        if (uarr[i] == NULL)
            break;
        if (i == MAXARG)
            return -E2BIG;
        int len = fetch_string(uarr[i], *pool, pool_end - *pool);
        if (len < 0)
            return len;
        karr[i] = *pool;
        *pool += len + 1;
    }
    karr[i] = NULL;
    return 0;
}

static int do_exec(const char *upath, char *const *uargv, char *const *uenvp)
{
    uint32_t page = pmm_alloc();
    if (!page)
        return -ENOMEM;

    struct exec_args *a = P2V(page);
    char *pool = a->pool, *pool_end = (char *)a + PAGE_SIZE;
    int err = fetch_string(upath, a->path, PATH_MAX);
    if (err >= 0)
        err = fetch_string_array(uargv, a->argv, &pool, pool_end);
    if (err >= 0 && uenvp)
        err = fetch_string_array(uenvp, a->envp, &pool, pool_end);
    else if (err >= 0)
        a->envp[0] = NULL;

    if (err >= 0)
        err = exec(a->path, a->argv, a->envp);
    pmm_free(page);
    return err < 0 ? err : 0;
}

/* exec(path, argv) */
int32_t sys_exec(struct trapframe *tf)
{
    return do_exec((const char *)tf->ebx, (char *const *)tf->ecx, NULL);
}

/* exece(path, argv, envp) */
int32_t sys_exece(struct trapframe *tf)
{
    return do_exec((const char *)tf->ebx, (char *const *)tf->ecx,
                   (char *const *)tf->edx);
}
