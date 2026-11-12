/* bootmod.c -- remember the boot modules' names and locations. */
#include "proc/bootmod.h"
#include "mm/memlayout.h"
#include "lib/string.h"
#include "lib/kprintf.h"

#define MAX_MODULES 16
#define NAME_LEN    32

struct bootmod {
    char        name[NAME_LEN];
    const char *data;           /* in the direct map */
    uint32_t    size;
};

static struct bootmod mods[MAX_MODULES];
static int nmods;

/* "build/user/bin/init" -> "init" */
static const char *basename(const char *path)
{
    const char *base = path;
    for (; *path; path++)
        if (*path == '/')
            base = path + 1;
    return base;
}

void bootmod_init(struct multiboot_info *mbi)
{
    if (!(mbi->flags & MB_INFO_MODS))
        return;

    struct multiboot_module *m = P2V(mbi->mods_addr);
    for (uint32_t i = 0; i < mbi->mods_count && nmods < MAX_MODULES; i++) {
        struct bootmod *b = &mods[nmods++];
        strncpy(b->name, basename(P2V(m[i].cmdline)), NAME_LEN - 1);
        b->data = P2V(m[i].mod_start);
        b->size = m[i].mod_end - m[i].mod_start;
        kprintf("bootmod: %s (%lu bytes)\n", b->name, b->size);
    }
}

int bootmod_find(const char *name, const char **data, uint32_t *size)
{
    name = basename(name);                  /* "/bin/hello" finds "hello" */
    for (int i = 0; i < nmods; i++) {
        if (strcmp(mods[i].name, name) == 0) {
            *data = mods[i].data;
            *size = mods[i].size;
            return 0;
        }
    }
    return -1;
}
