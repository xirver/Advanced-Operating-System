#include <cpu.h>
#include <error.h>
#include <list.h>
#include <task.h>
#include <rbtree.h>

#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/monitor.h>
#include <kernel/sched.h>
#include <kernel/vma.h>
#include <kernel/dev/rmap.h>

#define DEBUG 0

extern struct list runq;
extern size_t nuser_tasks;

/*
 * Create new page for the pml4 and copy the kernel to the new address space
 */
int create_pml4(struct task *task) 
{
	struct page_info *page;
	struct page_table *pml4;

	/* Allocate a page for the page table. */
	page = page_alloc(ALLOC_ZERO);
	if (!page) {
		return -1;
	}

	++page->pp_ref;
	pml4 = (struct page_table *) page2kva(page);

	task->task_pml4 = pml4;

	// Copy all page table entries from kernel space
	for (size_t i = PML4_INDEX(KERNEL_VMA); i < PAGE_TABLE_ENTRIES; ++i) {
		memcpy((void *) &task->task_pml4->entries[i], (void *) &kernel_pml4->entries[i], sizeof(physaddr_t));
	}

	return 0;
}

/*
 * Copy mappings from parent to child task. Don't create new physical pages, just point to the 
 * same pages as the parent and perform COW. Set the pages to read-only
 */
int copy_page_range(struct task *parent_task, struct task *child_task, void *start_va, void *end_va)
{
	void *va;
	struct page_info *page;
	physaddr_t *entry = kmalloc(sizeof(physaddr_t *));
	uint64_t page_flags;

	for (va = start_va; va < end_va; va += PAGE_SIZE) {
		page = page_lookup(parent_task->task_pml4, va, &entry);
		if(!page || !entry){
			continue;
		}

		if (!(*entry & PAGE_PRESENT))
			continue;

		if (*entry & PAGE_WRITE) {
			page_flags = PAGE_PRESENT | PAGE_NO_EXEC | PAGE_USER;
		} else if (!(*entry & PAGE_NO_EXEC)) {
			page_flags = PAGE_PRESENT | PAGE_USER;
		}

		// Set pages as read-only
		if (page_insert(child_task->task_pml4, page, va, page_flags) < 0) {
			cprintf("[copy_page_range]: Error - page-insert failed\n");
			return -1;
		}

		// Set parent page to read-only
		protect_region(parent_task->task_pml4, va, PAGE_SIZE, page_flags);
	}

	return 0;
}

/* 
 * Allocates a task struct for the child process and copies the register state,
 * the VMAs and the page tables. Once the child task has been set up, it is
 * added to the run queue.
 */
struct task *task_clone(struct task *task)
{
	struct task *child_task;
	struct vma *parent_vma, *child_vma;
	struct list *node;
	struct rmap *rmap;

	child_task = task_alloc(task->task_pid);

	// Copy the register state from parent to child
	memcpy(&child_task->task_frame, &task->task_frame, sizeof(struct int_frame));

	if (create_pml4(child_task) < 0)
		return NULL;

	// Copy VMAs
	list_init(&child_task->task_mmap);

	list_foreach(&task->task_mmap, node) {
		parent_vma = container_of(node, struct vma, vm_mmap);
		child_vma = kmalloc(sizeof(struct vma));
		
		if(!child_vma){
			cprintf("[task_clone]: Error: kmalloc failed\n");
			return NULL;
		}
		
		memcpy(child_vma, parent_vma, sizeof(struct vma));
		list_init(&child_vma->vm_mmap);
		rb_node_init(&child_vma->vm_rb);

		// Add reverse mapping node
		rmap = child_vma->rmap;
		spin_lock(&rmap->lock);
		list_add(&rmap->vmas, &child_vma->rmap_node);
		spin_unlock(&rmap->lock);

		insert_vma(child_task, child_vma);

		// Add the child to the parent's list of children
		list_add(&task->task_children, &child_task->task_child);

		// Copy pages in this VMA and set them to read-only. They will point to the same
		// physical addresses as the parent, so we increase pp_ref by 1 for each physical page
		copy_page_range(task, child_task, parent_vma->vm_base, parent_vma->vm_end);
	}

	// Copy jiffy count
	child_task->jiffies = task->jiffies;

	// Child will return pid = 0
	child_task->task_frame.rax = 0;

	return child_task;
}

pid_t sys_fork(void)
{
	/* LAB 5: your code here. */
	struct task *child_task;

	cprintf("\n\n\tsys_fork\n\n");

	child_task = task_clone(cur_task);
	if (!child_task) {
		return -1;
	}

	queue_add_task(&runq, child_task);
	nuser_tasks++;
	//runq_add_task(child_task);

	return child_task->task_pid;
}

