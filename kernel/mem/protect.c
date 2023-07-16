#include <types.h>
#include <paging.h>

#include <kernel/mem.h>

#define DEBUG 0

struct protect_info {
	struct page_table *pml4;
	uint64_t flags;
	uintptr_t base, end;
};

/* Changes the protection of the page. Avoid calling tlb_invalidate() if
 * nothing changes at all.
 */
static int protect_pte(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct protect_info *info = walker->udata;

	// Set permissions
	uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_NO_EXEC;

	if (DEBUG) cprintf("[protect_region]: [%p, %p] before (R: %d, W: %d, X: %d, U: %d)\n", 
		base, end, (*entry & PAGE_PRESENT) != 0, (*entry & PAGE_WRITE) != 0, (!(*entry & PAGE_NO_EXEC)) != 0, (*entry & PAGE_USER) != 0);

	// Check if the flags are changing
	if ((*entry & flags) != info->flags) {
		// Zero out the flags
		*entry = *entry & (~flags);
		//*entry ^= flags;

		// Set new flags
		*entry |= info->flags;
		*entry |= PAGE_PRESENT;

		tlb_invalidate(info->pml4, (void *) base);
	}

	if (DEBUG) cprintf("[protect_region]: [%p, %p] after (R: %d, W: %d, X: %d, U: %d)\n", 
		base, end, (*entry & PAGE_PRESENT) != 0, (*entry & PAGE_WRITE) != 0, (!(*entry & PAGE_NO_EXEC)) != 0, (*entry & PAGE_USER) != 0);

	return 0;
}

/* Changes the protection of the huge page, if the page is a huge page and if
 * the range covers the full huge page. Otherwise if the page is a huge page,
 * but if the range does not span an entire huge page, this function calls
 * ptbl_split() to split up the huge page. Avoid calling tlb_invalidate() if
 * nothing changes at all.
 */
static int protect_pde(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct protect_info *info = walker->udata;

	/* LAB 3: your code here. */
	return 0;
}

/* Changes the protection of the region [va, va + size) to the permissions
 * specified by flags.
 */
void protect_region(struct page_table *pml4, void *va, size_t size,
    uint64_t flags)
{
	/* LAB 3: your code here. */
	struct protect_info info = {
		.pml4 = pml4,
		.flags = flags,
		.base = ROUNDDOWN((uintptr_t)va, PAGE_SIZE),
		.end = ROUNDUP((uintptr_t)va + size, PAGE_SIZE) - 1,
	};
	struct page_walker walker = {
		.pte_callback = protect_pte,
		.pde_callback = protect_pde,
		.udata = &info,
	};

	if (DEBUG) cprintf("[protect_region]: [%p, %p] (R: %d, W: %d, X: %d, U: %d)\n", 
		info.base, info.end, (flags & PAGE_PRESENT) != 0, (flags & PAGE_WRITE) != 0, (!(flags & PAGE_NO_EXEC)) != 0, (flags & PAGE_USER) != 0);


	walk_page_range(pml4, va, (void *)((uintptr_t)va + size), &walker);
}
