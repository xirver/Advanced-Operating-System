#include <types.h>
#include <paging.h>

#include <kernel/mem.h>
#include <kernel/vma.h>

#include <include/elf.h>

#define DEBUG 0

struct boot_map_info {
	struct page_table *pml4;
	uint64_t flags;
	physaddr_t pa;
	uintptr_t base, end;
};

/* Stores the physical address and the appropriate permissions into the PTE and
 * increments the physical address to point to the next page.
 */
static int boot_map_pte(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct boot_map_info *info = walker->udata;
	
	// Zero out the entire entry
	*entry = 0;

	// Set the new address of the PTE to the physical address of the page
	*entry = info->pa;

	// Set permissions
	*entry |= info->flags;

	// Set status to mapped if not already done by the flags above
	*entry |= PAGE_PRESENT;

	//increment pa to point to next page
	info->pa += PAGE_SIZE;
	
	return 0;
}

/* Stores the physical address and the appropriate permissions into the PDE and
 * increments the physical address to point to the next huge page if the
 * physical address is huge page aligned and if the area to be mapped covers a
 * 2M area. Otherwise this function calls ptbl_split() to split down the huge
 * page or allocate a page table.
 */
static int boot_map_pde(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct boot_map_info *info = walker->udata;
	return ptbl_alloc(entry, base, end, walker);
}

static int boot_map_pdpte(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	return ptbl_alloc(entry, base, end, walker);
}

static int boot_map_pml4e(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	return ptbl_alloc(entry, base, end, walker);
}

/*
 * Maps the virtual address space at [va, va + size) to the contiguous physical
 * address space at [pa, pa + size). Size is a multiple of PAGE_SIZE. The
 * permissions of the page to set are passed through the flags argument.
 *
 * This function is only intended to set up static mappings. As such, it should
 * not change the reference counts of the mapped pages.
 *
 * Hint: this function calls walk_page_range().
 */
void boot_map_region(struct page_table *pml4, void *va, size_t size,
    physaddr_t pa, uint64_t flags)
{
	/* LAB 2: your code here. */
	struct boot_map_info info = {
		.pa = pa,
		.flags = flags,
		.base = ROUNDDOWN((uintptr_t)va, PAGE_SIZE),
		.end = ROUNDUP((uintptr_t)va + size, PAGE_SIZE) - 1,
		.pml4 = pml4,
	};
	struct page_walker walker = {
		.pte_callback = boot_map_pte,
		.pde_callback = boot_map_pde,
		.pdpte_callback = boot_map_pdpte,
		.pml4e_callback = boot_map_pml4e,
		.udata = &info,
	};


	if (DEBUG) cprintf("[boot_map_region]: creating a mapping from va = [%p, %p] to pa = [%p, %p]\n\tsize = %d\n\n", info.base, info.end, pa, pa+size, size);

	walk_page_range(pml4, va, (void *)((uintptr_t)va + size), &walker);
}

/* Creates a mapping in the MMIO region to [pa, pa + size) for
 * memory-mapped I/O.
 */
void *mmio_map_region(physaddr_t pa, size_t size)
{
	static uintptr_t base = MMIO_BASE;
	void *ret;

	size = ROUNDUP(size, PAGE_SIZE);
	assert(base + size < MMIO_LIM);

	ret = (void *)base;
	boot_map_region(kernel_pml4, ret, size, pa, PAGE_PRESENT |
		PAGE_WRITE | PAGE_NO_EXEC | PAGE_WRITE_THROUGH | PAGE_NO_CACHE);
	base += size;

	return ret;
}

int convert_flags_from_pages_to_vma(uint64_t page_flags)
{
	uint64_t vma_flags = 0;

	if(page_flags & PAGE_PRESENT) {
		vma_flags |= VM_READ;
	}
	if(page_flags & PAGE_WRITE) {
		vma_flags |= VM_WRITE;
	}
	if(!(page_flags & PAGE_NO_EXEC)) {
		vma_flags |= VM_EXEC;
	}

	return vma_flags;
}

uint64_t convert_flags_from_vma_to_pages(int vma_flags)
{
	uint64_t page_flags = 0;

	if(vma_flags & VM_READ) {
		page_flags |= PAGE_PRESENT;
	}
	if(vma_flags & VM_WRITE) {
		page_flags |= PAGE_WRITE;
	}
	if(!(vma_flags & VM_EXEC)) {
		page_flags |= PAGE_NO_EXEC;
	}

	return page_flags;
}

uint64_t convert_flags_from_elf_to_pages(struct elf_proghdr *hdr)
{
	uint64_t page_flags;

	page_flags = PAGE_PRESENT;

	if(hdr->p_flags & ELF_PROG_FLAG_WRITE){
		page_flags |= PAGE_WRITE;
	}

	if(!(hdr->p_flags & ELF_PROG_FLAG_EXEC)){
		page_flags |= PAGE_NO_EXEC;
	}

	return page_flags;
}

/* This function parses the program headers of the ELF header of the kernel
 * to map the regions into the page table with the appropriate permissions.
 *
 * First creates an identity mapping at the KERNEL_VMA of size BOOT_MAP_LIM
 * with permissions RW-.
 *
 * Then iterates the program headers to map the regions with the appropriate
 * permissions.
 *
 * Hint: this function calls boot_map_region().
 * Hint: this function ignores program headers below KERNEL_VMA (e.g. ".boot").
 */
void boot_map_kernel(struct page_table *pml4, struct elf *elf_hdr)
{
	struct elf_proghdr *prog_hdr =
	    (struct elf_proghdr *)((char *)elf_hdr + elf_hdr->e_phoff);
	uint64_t flags;
	size_t i;

	if (DEBUG) {
		cprintf("\n\n-------------------------------------------------------------\n");
		cprintf("        Creating identity mapping for kernel\n");
		cprintf("-------------------------------------------------------------\n\n");
	}

	// Create identity mapping at KERNEL_VMA of size BOOT_MAP_LIM with RW-
	flags = (PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXEC); 

	boot_map_region(pml4, (void *)KERNEL_VMA, BOOT_MAP_LIM, 0, flags);

	if (DEBUG) {
		cprintf("\n\n-------------------------------------------------------------\n");
		cprintf("              Mapping ELF program headers\n");
		cprintf("-------------------------------------------------------------\n\n");
	}

	// Map ELF program headers
	struct elf_proghdr *hdr;
	for(i = 0; i < elf_hdr->e_phnum; i++){
		hdr = prog_hdr + i;
		if((void *) (hdr)->p_va > (void *) KERNEL_VMA){
			// Convert flags from ELF to pages
			flags = convert_flags_from_elf_to_pages(hdr);

			// Map the ELF program headers from VA -> PA
			boot_map_region(pml4, (void *) hdr->p_va, hdr->p_memsz, hdr->p_pa, flags);
		}
	}
}
