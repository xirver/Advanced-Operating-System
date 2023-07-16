#include <types.h>
#include <string.h>
#include <paging.h>

#include <kernel/mem.h>

#define DEBUG 0

/* Allocates a page table if none is present for the given entry.
 * If there is already something present in the PTE, then this function simply
 * returns. Otherwise, this function allocates a page using page_alloc(),
 * increments the reference count and stores the newly allocated page table
 * with the PAGE_PRESENT | PAGE_WRITE | PAGE_USER permissions.
 */
int ptbl_alloc(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct page_info *page;
	uint64_t flags;

	if (DEBUG) cprintf("[ptbl_alloc]: allocating new page table\n");

	if(*entry & PAGE_PRESENT){
		return 0;
	}

	// Create new page table for the entry
	page = page_alloc(ALLOC_ZERO);
	if (!page) {
		return -1;
	}

	page->pp_ref += 1;

	// Zero out the entire entry
	*entry = 0;

	// Set the new address of the PDE to the physical address of the page
	*entry = (uintptr_t) page2pa(page);
	
	// Set page to mapped and add permission
	flags = (PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	*entry |= flags;

	return 0; 
}

/* Splits up a huge page by allocating a new page table and setting up the huge
 * page into smaller pages that consecutively make up the huge page.
 *
 * If no huge page was mapped at the entry, simply allocate a page table.
 *
 * Otherwise if a huge page is present, allocate a new page, increment the
 * reference count and have the PDE point to the newly allocated page. This
 * page is used as the page table. Then allocate a normal page for each entry,
 * copy over the data from the huge page and set each PDE.
 *
 * Hint: the only user of this function is boot_map_region(). Otherwise the 2M
 * physical page has to be split down into its individual 4K pages by updating
 * the respective struct page_info structs.
 *
 * Hint: this function calls ptbl_alloc(), page_alloc(), page2pa() and
 * page2kva().
 */
int ptbl_split(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	/* LAB 2: your code here. */
	return 0;
}

/* Attempts to merge all consecutive pages in a page table into a huge page.
 *
 * First checks if the PDE points to a huge page. If the PDE points to a huge
 * page there is nothing to do. Otherwise the PDE points to a page table.
 * Then this function checks all entries in the page table to check if they
 * point to present pages and share the same flags. If not all pages are
 * present or if not all flags are the same, this function simply returns.
 * At this point the pages can be merged into a huge page. This function now
 * allocates a huge page and copies over the data from the consecutive pages
 * over to the huge page.
 * Finally, it sets the PDE to point to the huge page with the flags shared
 * between the previous pages.
 *
 * Hint: don't forget to free the page table and the previously used pages.
 */
int ptbl_merge(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	/* LAB 2: your code here. */
	return 0;
}

/* Frees up the page table by checking if all entries are clear. Returns if no
 * page table is present. Otherwise this function checks every entry in the
 * page table and frees the page table if no entry is set.
 *
 * Hint: this function calls pa2page(), page2kva() and page_free().
 */
int ptbl_free(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct page_table *pt;
	struct page_info *page;
	physaddr_t pt_entry, pa;

	// Check if page table is present
	if(!(*entry & PAGE_PRESENT)){
		return 0;
	}

	pt = (struct page_table *) KADDR(PAGE_ADDR(*entry));
	for (int i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
		pt_entry = pt->entries[i];
		if (pt_entry & PAGE_PRESENT) {
			return 0;
		}
	}

	// Page table has no present entries, so we can free it
	pa = PAGE_ADDR(*entry);
	page = pa2page(pa);
	page->pp_ref--;
	page->pp_free = 1;
	tlb_invalidate(pt,pt);
	*entry = 0;
	page_free(page);

	return 0;
}
