#include <types.h>
#include <paging.h>
#include <vma.h>
#include <kernel/mem.h>
#include <kernel/vma.h>
#include <kernel/sched/task.h>
#include <kernel/dev/swap.h>

#include <kernel/mem.h>

#define DEBUG 0

extern struct swap_info swap;

struct populate_info {
	uint64_t flags;
	uintptr_t base, end;
};

static int populate_pte(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct page_info *page;
	struct populate_info *info = walker->udata;
	struct vma *vma;

	// Sanity check - there should be no present pages at the given va
	assert(!(*entry & PAGE_PRESENT));

	page = page_alloc(ALLOC_ZERO);
	if (!page) {
		return -1;
	}

	// Write the rmap of the VMA in the page struct
	//if (cur_task) {
	if (info->flags & PAGE_USER) {
		assert(cur_task);
		vma = task_find_vma(cur_task, (void *) base);
		if (!vma) {
			panic("no vma\n");
			return -1;
		}
		page->rmap = vma->rmap;

		spin_lock(&swap.lock);
		add_swap_page(page);
		spin_unlock(&swap.lock);
		
	} else {
		// Kernel pages don't get swapped
		page->rmap = NULL;
	}

	// Increment ref count of new page
	page->pp_ref += 1;

	// Zero out the entire entry
	*entry = 0;

	// Set the new address of the PTE to the physical address of the page
	*entry = (uintptr_t) page2pa(page); 

	// Set permissions
	*entry |= info->flags;

	// Set status to mapped if not already done by the flags above
	*entry |= PAGE_PRESENT;

	tlb_invalidate(kernel_pml4, (void *) base);

	return 0;
}

static int populate_pde(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct page_info *page;
	struct populate_info *info = walker->udata;

	return 0;
}

/* Populates the region [va, va + size) with pages by allocating pages from the
 * frame allocator and mapping them.
 */
void populate_region(struct page_table *pml4, void *va, size_t size,
	uint64_t flags)
{
	struct populate_info info = {
		.flags = flags,
		.base = ROUNDDOWN((uintptr_t)va, PAGE_SIZE),
		.end = ROUNDUP((uintptr_t)va + size, PAGE_SIZE) - 1,
	};
	struct page_walker walker = {
		.pte_callback = populate_pte,
		.pde_callback = ptbl_alloc,
		.pdpte_callback = ptbl_alloc,
		.pml4e_callback = ptbl_alloc,
		.udata = &info,
	};

	if (DEBUG) cprintf("[populate_region]: [%p, %p] (R: %d, W: %d, X: %d, U: %d)\n", 
		info.base, info.end, (flags & PAGE_PRESENT) != 0, (flags & PAGE_WRITE) != 0, (!(flags & PAGE_NO_EXEC)) != 0, (flags & PAGE_USER) != 0);


	walk_page_range(pml4, va, (void *)((uintptr_t)va + size), &walker);
}
