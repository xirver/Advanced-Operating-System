#include <error.h>
#include <string.h>
#include <paging.h>
#include <task.h>
#include <cpu.h>
#include <rbtree.h>
#include <atomic.h>

#include <kernel/acpi.h>
#include <include/elf.h>
#include <kernel/monitor.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/vma/insert.h>
#include <kernel/vma/populate.h>
#include <kernel/vma/show.h>
#include <kernel/vma/remove.h>
#include <kernel/mem/dump.h>
#include <kernel/sched/wait.h>
#include <kernel/sched/kernel_thread.h>

extern struct task **tasks;
extern struct spinlock runq_lock;
extern size_t nuser_tasks;
extern size_t nkernel_tasks;
extern struct list runq;
extern struct list zero_list;
extern pid_t pid_max;

void zero_page(struct page_info *page)
{
	uint64_t paddr = page2pa(page);

	memset((void *) paddr, 0, PAGE_SIZE);
}

/* When a page is freed, set the first byte of the page to 1. When we free a page
 * set this byte and all other bytes to 0
 *
 * TODO: set first byte to 1 in page free
 *  
 */
void zero_all_pages(void)
{
	struct list *free_node;
	struct page_info *free_page;

	lock_buddy();

	cprintf("\n\n\tzero\n\n");

	free_node = list_pop(&zero_list);
	while (free_node) {
		free_page = container_of(free_node, struct page_info, pp_zero_node);
		zero_page(free_page);
		free_page->pp_zero = 1;
		free_node = list_pop(&zero_list);
	}

	unlock_buddy();

	cur_task->task_frame.rip = (uint64_t) &zero_all_pages;
	cur_task->task_frame.rsp = KERNEL_STACK_TOP;

	sched_yield();
}

void create_kernel_thread(uint64_t func_ptr)
{
	pid_t pid;
	struct spinlock *lock;
	struct task *task = task_alloc(0);

	if (!task) {
		panic("Error: task_alloc failed\n");
	}

	task->task_type = TASK_TYPE_KERNEL;
	task->task_frame.rip = func_ptr;
	task->task_frame.ds = GDT_KDATA;
	task->task_frame.ss = GDT_KDATA;
	task->task_frame.rsp = KERNEL_STACK_TOP;
	task->task_frame.cs = GDT_KCODE;
	task->task_frame.rflags = IF_RFLAGS;

	populate_region(task->task_pml4, (void *) (KERNEL_STACK_TOP - ((nkernel_tasks + 1) * PAGE_SIZE)), PAGE_SIZE, PAGE_PRESENT | PAGE_WRITE);

	// Get a PID that's not used for the tests
	tasks[task->task_pid] = NULL;

	// Get the highest PID possible
	for (pid = pid_max - 1; pid > 1; pid--) {
		if (!tasks[pid]) {
			tasks[pid] = task;
			task->task_pid = pid;
			break;
		}
	}

	// We are out of PIDs 
	if (pid == 1) {
		kfree(task);
        panic("Error: no PIDs remaining to create kernel thread\n");
	}

    lock_runq_add(task);
}