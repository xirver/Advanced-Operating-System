#include <types.h>
#include <paging.h>

#include <kernel/mem.h>
#include <kernel/vma.h>
#include <vma.h>
#include <kernel/sched/task.h>
#include <kernel/dev/swap.h>

#define DEBUG 0

extern struct swap_info swap;

struct insert_info {
	struct page_table *pml4;
	struct page_info *page;
	uint64_t flags;
};

/* If the PTE already points to a present page, the reference count of the page
 * gets decremented and the TLB gets invalidated. Then this function increments
 * the reference count of the new page and sets the PTE to the new page with
 * the user-provided permissions.
 */
static int insert_pte(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct insert_info *info = walker->udata;
	struct page_info *page = info->page;
	struct page_info *old_page;
	struct vma *vma;
	struct rmap *rmap;


	// Check if page is already in the PTE - decrement its ref and invalidate TLB
	if (*entry & PAGE_PRESENT) {
		old_page = (struct page_info *) pa2page(PAGE_ADDR(*entry));
		page_decref(old_page);
		tlb_invalidate(info->pml4, (void *)base);
	}

	// Write the rmap of the VMA in the page struct
	if (info->flags & PAGE_USER) {
		assert(cur_task);
		assert(base < USER_LIM);
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

	return 0;
}

/* If the PDE already points to a present huge page, the reference count of the
 * huge page gets decremented and the TLB gets invalidated. Then if the new
 * page is a 4K page, this function calls ptbl_alloc() to allocate a new page
 * table. If the new page is a 2M page, this function increments the reference
 * count of the new page and sets the PDE to the new huge page with the
 * user-provided permissions.
 */
static int insert_pde(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	if (DEBUG) cprintf("[insert_pde]: checking to allocate new pt table\n\n");
	struct insert_info *info = walker->udata;
	struct page_info *page;

#ifdef BONUS_LAB2
	// Check if PDE already points to a huge page - decrement its ref and invalidate TLB
	if (*entry & PAGE_HUGE && *entry & PAGE_PRESENT) {
		page = (struct page_info *) PAGE_ADDR(*entry);
		page->pp_ref -= 1;
		tlb_invalidate(info->pml4, (void *)base);
	}
#endif

	ptbl_alloc(entry, base, end, walker);

	return 0;
}

static int insert_pdpte(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	if (DEBUG) cprintf("[insert_pdpte]: checking to allocate new pdir table\n\n");
	struct insert_info *info = walker->udata;
	struct page_info *page;

	ptbl_alloc(entry, base, end, walker);

	return 0;
}

static int insert_pml4e(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	if (DEBUG) cprintf("[insert_plm4e]: checking to allocate new pdpte table\n\n");
	struct insert_info *info = walker->udata;
	struct page_info *page;

	ptbl_alloc(entry, base, end, walker);

	return 0;
}


/* Map the physical page page at virtual address va. The flags argument
 * contains the permission to set for the PTE. The PAGE_PRESENT flag should
 * always be set.
 *
 * Requirements:
 *  - If there is already a page mapped at va, it should be removed using
 *    page_decref().
 *  - If necessary, a page should be allocated and inserted into the page table
 *    on demand. This can be done by providing ptbl_alloc() to the page walker.
 *  - The reference count of the page should be incremented upon a successful
 *    insertion of the page.
 *  - The TLB must be invalidated if a page was previously present at va.
 *
 * Corner-case hint: make sure to consider what happens when the same page is
 * re-inserted at the same virtual address in the same page table. However, do
 * not try to distinguish this case in your code, as this frequently leads to
 * subtle bugs. There is another elegant way to handle everything in the same
 * code path.
 *
 * Hint: what should happen when the user inserts a 2M huge page at a
 * misaligned address?
 *
 * Hint: this function calls walk_page_range(), hpage_aligned()and page2pa().
 */
int page_insert(struct page_table *pml4, struct page_info *page, void *va,
    uint64_t flags)
{
	struct insert_info info = {
		.pml4 = pml4,
		.page = page,
		.flags = flags,
	};
	struct page_walker walker = {
		.pte_callback = insert_pte,
		.pde_callback = insert_pde,
		.pdpte_callback = insert_pdpte,
		.pml4e_callback = insert_pml4e,
		.udata = &info,
	};

	return walk_page_range(pml4, va, (void *)((uintptr_t)va + PAGE_SIZE),
		&walker);
}

