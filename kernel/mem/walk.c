#include <types.h>
#include <paging.h>

#include <kernel/mem.h>

#define DEBUG 0

/* Given an address addr, this function returns the sign extended address. */
static uintptr_t sign_extend(uintptr_t addr)
{
	return (addr < USER_LIM) ? addr : (0xffff000000000000ull | addr);
}

/* Given an addresss addr, this function returns the page boundary. */
static uintptr_t ptbl_end(uintptr_t addr)
{
	return addr | (PAGE_SIZE - 1);
}

/* Given an address addr, this function returns the page table boundary. */
static uintptr_t pdir_end(uintptr_t addr)
{
	return addr | (PAGE_TABLE_SPAN - 1);
}

/* Given an address addr, this function returns the page directory boundary. */
static uintptr_t pdpt_end(uintptr_t addr)
{
	return addr | (PAGE_DIR_SPAN - 1);
}

/* Given an address addr, this function returns the PDPT boundary. */
static uintptr_t pml4_end(uintptr_t addr)
{
	return addr | (PDPT_SPAN - 1);
}

/* Walks over the page range from base to end iterating over the entries in the
 * given page table ptbl. The user may provide walker->pte_callback() that gets
 * called for every entry in the page table. In addition the user may provide
 * walker->pt_hole_callback() that gets called for every unmapped entry in the page
 * table.
 *
 * Hint: this function calls ptbl_end() to get the end boundary of the current
 * page.
 * Hint: the next page is at ptbl_end() + 1.
 * Hint: the loop condition is next < end.
 */

static int ptbl_walk_range(struct page_table *ptbl, uintptr_t base,
    uintptr_t end, struct page_walker *walker)
{
	map_pte_t callback;
	physaddr_t *entry;
	int ret;
	uintptr_t idx, addr, next;

	next = base;
	for (addr = sign_extend(base); next < end; addr = next) {
		next = sign_extend(ptbl_end(addr) + 1); 
		if (next > end || next < addr) // Catch overflow
			next = end;

		// Get the entry in the PDPT table
		idx = PAGE_TABLE_INDEX(addr);
		entry = &ptbl->entries[idx];

		// Callback for each entry
		if (walker->pte_callback) {
			ret = walker->pte_callback(entry, addr, next - 1, walker);
			if (ret < 0) {
				return ret;
			}
		}

		// Check if entry is mapped
		if (*entry & PAGE_PRESENT) {
			if (walker->pte_unmap) {
				ret = walker->pte_unmap(entry, addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
			}
		} else {
			// Callback for each unmapped entry
			if (walker->pt_hole_callback) {
				ret = walker->pt_hole_callback(addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}
	return 0;
}

/* Walks over the page range from base to end iterating over the entries in the
 * given page directory pdir. The user may provide walker->pde_callback() that gets
 * called for every entry in the page directory. In addition the user may
 * provide walker->pt_hole_callback() that gets called for every unmapped entry in the
 * page directory. If the PDE is present, but not a huge page, this function
 * calls ptbl_walk_range() to iterate over the entries in the page table. The
 * user may provide walker->pde_unmap() that gets called for every present PDE
 * after walking over the page table.
 *
 * Hint: see ptbl_walk_range().
 */
static int pdir_walk_range(struct page_table *pdir, uintptr_t base,
    uintptr_t end, struct page_walker *walker)
{
	/* LAB 2: your code here. */
	map_pte_t callback;
	physaddr_t *entry;
	int ret;
	uintptr_t idx, addr, next;

	next = base;
	for (addr = sign_extend(base); next < end; addr = next) {
		next = sign_extend(pdir_end(addr) + 1);
		if (next > end || next < addr) // Catch overflow
			next = end;

		// Get the entry in the PDPT table
		idx = PAGE_DIR_INDEX(addr);
		entry = &pdir->entries[idx];

		// Callback for each entry
		if (walker->pde_callback) {
			ret = walker->pde_callback(entry, addr, next - 1, walker);
			if (ret < 0) {
				return ret;
			}
		}

		// Check if entry is mapped
		if (*entry & PAGE_PRESENT) {
			// Walk pdir table if not huge page
			if (!(*entry & PAGE_HUGE)) { 
				ret = ptbl_walk_range((struct page_table *) KADDR(PAGE_ADDR(*entry)), addr, next - 1, walker);

				if (ret < 0) {
					return ret;
				}
				if (walker->pde_unmap) {
					ret = walker->pde_unmap(entry, addr, next - 1, walker);
					if (ret < 0) {
						return ret;
					}
				}
			}
		} else {
			// Callback for each unmapped entry
			if (walker->pt_hole_callback) {
				ret = walker->pt_hole_callback(addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}

	return 0;
}

/* Walks over the page range from base to end iterating over the entries in the
 * given PDPT pdpt. The user may provide walker->pdpte_callback() that gets called
 * for every entry in the PDPT. In addition the user may provide
 * walker->pt_hole_callback() that gets called for every unmapped entry in the PDPT. If
 * the PDPTE is present, but not a large page, this function calls
 * pdir_walk_range() to iterate over the entries in the page directory. The
 * user may provide walker->pdpte_unmap() that gets called for every present
 * PDPTE after walking over the page directory.
 *
 * Hint: see ptbl_walk_range().
 */
static int pdpt_walk_range(struct page_table *pdpt, uintptr_t base,
    uintptr_t end, struct page_walker *walker)
{
	map_pte_t callback;
	physaddr_t *entry;
	int ret;
	uintptr_t idx, addr, next;

	next = base;
	for (addr = sign_extend(base); next < end; addr = next) {
		next = sign_extend(pdpt_end(addr) + 1);
		if (next > end || next < addr) // Catch overflow
			next = end;

		// Get the entry in the PDPT table
		idx = PDPT_INDEX(addr);
		entry = &pdpt->entries[idx];

		// Callback for each entry
		if (walker->pdpte_callback) {
			ret = walker->pdpte_callback(entry, addr, next - 1, walker);
			if (ret < 0) {
				return ret;
			}
		}
		
		// Check if entry is mapped
		if (*entry & PAGE_PRESENT) {
			// Walk pdir table if not huge page
			if (!(*entry & PAGE_HUGE)) {
				ret = pdir_walk_range((struct page_table *) KADDR(PAGE_ADDR(*entry)), addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
				if (walker->pdpte_unmap) {
					ret = walker->pdpte_unmap(entry, addr, next - 1, walker);
					if (ret < 0) {
						return ret;
					}
				}
			}
		} else {
			// Callback for each unmapped entry
			if (walker->pt_hole_callback) {
				ret = walker->pt_hole_callback(addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}

	return 0;
}

/* Walks over the page range from base to end iterating over the entries in the
 * given PML4 pml4. The user may provide walker->pml4e_callback() that gets called
 * for every entry in the PML4. In addition the user may provide
 * walker->pt_hole_callback() that gets called for every unmapped entry in the PML4. If
 * the PML4E is present, this function calls pdpt_walk_range() to iterate over
 * the entries in the PDPT. The user may provide walker->pml4e_unmap() that
 * gets called for every present PML4E after walking over the PDPT.
 *
 * Hint: see ptbl_walk_range().
 */
static int pml4_walk_range(struct page_table *pml4, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct page_table *pt;
	map_pte_t callback;
	physaddr_t *entry;
	int ret;
	uintptr_t idx, addr, next;

	next = base;
	for (addr = sign_extend(base); next < end; addr = next) {
		next = sign_extend(pml4_end(addr) + 1);
		if (next > end || next < addr) // Catch overflow
			next = end;

		// Get the entry in the PML4 table
		idx = PML4_INDEX(addr);
		entry = &pml4->entries[idx];

		// Callback for each entry
		if (walker->pml4e_callback) {
			ret = walker->pml4e_callback(entry, addr, next - 1, walker);
			if (ret < 0) {
				return ret;
			}
		}
		
		// Check if entry is mapped
		if (*entry & PAGE_PRESENT) {
			// Walk pdpt table 
			ret = pdpt_walk_range((struct page_table *) KADDR(PAGE_ADDR(*entry)), addr, next - 1, walker);
			if (ret < 0) {
				return ret;
			}
			if (walker->pml4e_unmap) {
				ret = walker->pml4e_unmap(entry, addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
			}
		} else {
			// Callback for each unmapped entry
			if (walker->pt_hole_callback) {
				ret = walker->pt_hole_callback(addr, next - 1, walker);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}

	return 0;
}

/* Helper function to walk over a page range starting at base and ending before
 * end.
 */
int walk_page_range(struct page_table *pml4, void *base, void *end,
	struct page_walker *walker)
{
	return pml4_walk_range(pml4, ROUNDDOWN((uintptr_t)base, PAGE_SIZE),
		ROUNDUP((uintptr_t)end, PAGE_SIZE) - 1, walker);
}

/* Helper function to walk over all pages. */
int walk_all_pages(struct page_table *pml4, struct page_walker *walker)
{
	return pml4_walk_range(pml4, 0, KERNEL_LIM, walker);
}

/* Helper function to walk over all user pages. */
int walk_user_pages(struct page_table *pml4, struct page_walker *walker)
{
	return pml4_walk_range(pml4, 0, USER_LIM, walker);
}

/* Helper function to walk over all kernel pages. */
int walk_kernel_pages(struct page_table *pml4, struct page_walker *walker)
{
	return pml4_walk_range(pml4, KERNEL_VMA, KERNEL_LIM, walker);
}

