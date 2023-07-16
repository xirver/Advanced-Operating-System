#include <types.h>

#include <kernel/mem.h>
#include <kernel/vma.h>
#include <kernel/dev/swap.h>

#define DEBUG 1

extern struct swap_info swap;

/*
 * Create a copy of a page for a new task
 */
int copy_on_write(struct task *task, void *va, struct page_info *page, physaddr_t *entry, struct vma *vma)
{
	struct page_info *new_page;

	if (DEBUG) cprintf("[copy_on_write]: PID %d | va: %p\n", task->task_pid, va);

	// If the page only has one reference to it, you can simply mark the page as writable.
	if(page->pp_ref == 1) {
		*entry |= PAGE_WRITE;
		return 0;
	}

	new_page = page_alloc(ALLOC_ZERO);
	if (!new_page) {
		cprintf("[copy_on_write]: page_alloc failed\n");
		return -1;
	}
	
	memcpy(page2kva(new_page), page2kva(page), PAGE_SIZE);

	// page_insert will decrement pp_ref of the old page and increment new page
	return page_insert(task->task_pml4, new_page, ROUNDDOWN(va, PAGE_SIZE), 
							convert_flags_from_vma_to_pages(vma->vm_flags) | PAGE_USER);
} 

/* Handles the page fault for a given task. */
int task_page_fault_handler(struct task *task, void *va, int flags)
{
	/* LAB 4: your code here. */
	struct vma *vma;
	struct page_info *page, *new_page;
	physaddr_t *entry = kmalloc(sizeof(physaddr_t *));
	int ret;

	vma = task_find_vma(task, va);
	if (!vma)
		return -1;

	page = page_lookup(task->task_pml4, ROUNDDOWN(va, PAGE_SIZE), &entry);

	if (page) {
		spin_lock(&swap.lock);
		// Add page to swap list if it's not already in it
		add_swap_page(page);
		// Move page to front of swap list - set as most recently used
		mru_swap_page(page);
		spin_unlock(&swap.lock);
	}

	if(page && *entry && (vma->vm_flags & VM_WRITE) && !(*entry & PAGE_WRITE)){
		ret = copy_on_write(task, va, page, entry, vma);
	} else {
		ret = populate_vma_range(task, ROUNDDOWN(va, PAGE_SIZE), PAGE_SIZE, flags);
	}

	return ret;
}
