#include <types.h>
#include <stdio.h>
#include <task.h>

struct rmap {
    struct list vmas;
    struct spinlock lock;
};

void rmap_walk(struct page_info *page, struct page_walker *walker);