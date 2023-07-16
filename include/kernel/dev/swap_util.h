#include <types.h>
#include <stdio.h>
#include <task.h>
#include <kernel/sched/task.h>


void mru_swap_page(struct page_info *page);
void remove_swap_page(struct page_info *page);
void add_swap_page(struct page_info *page);