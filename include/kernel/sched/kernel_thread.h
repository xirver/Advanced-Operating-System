#pragma once

#include <task.h>
#include <cpu.h>

// Use kernel stack in unused memory region
#define KERNEL_STACK_TOP 0xffffff9000000000

void zero_page(struct page_info *page);
void zero_all_pages(void);
void create_kernel_thread(uint64_t func_ptr);