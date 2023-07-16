#include <types.h>
#include <cpu.h>
#include <list.h>
#include <stdio.h>
#include <vma.h>

#include <kernel/mem/buddy.h>
#include <kernel/mem/walk.h>
#include <kernel/sched/task.h>
#include <kernel/dev/swap.h>
#include <kernel/dev/oom.h>
#include <kernel/dev/disk.h>
#include <kernel/dev/rmap.h>
#include <kernel/sched/kernel_thread.h>

#define DEBUG 1

extern struct swap_info swap;

void mru_swap_page(struct page_info *page) 
{
	if (!page)
		return;

	remove_swap_page(page);

	add_swap_page(page);
}

// NOTE: Does not lock the swap list
void remove_swap_page(struct page_info *page)
{
	if (!list_is_empty(&page->swap_node)) {
		list_del(&page->swap_node);
	}
}

void add_swap_page(struct page_info *page)
{
    struct list *node;

    // Don't add the page if it's already in the list
    if (!list_is_empty(&page->swap_node)){
        return;
    }
    list_add(&swap.pages, &page->swap_node);
}