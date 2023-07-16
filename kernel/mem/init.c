#include <types.h>
#include <boot.h>
#include <list.h>
#include <paging.h>
#include <cpu.h>

#include <x86-64/asm.h>

#include <kernel/mem.h>
#include <kernel/tests.h>

#define DEBUG 0

extern struct list buddy_free_list[];

/* The kernel's initial PML4. */
struct page_table *kernel_pml4;

/* This function sets up the initial PML4 for the kernel. */
int pml4_setup(struct boot_info *boot_info)
{
	struct page_info *page;

	uint64_t flags = (PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXEC);

	/* Allocate the kernel PML4. */
	page = page_alloc(ALLOC_ZERO);

	if (!page) {
		panic("unable to allocate the PML4!");
	}

	kernel_pml4 = page2kva(page);

	/* Map in the regions used by the kernel from the ELF header passed to
	 * us through the boot info struct.
	 */
	boot_map_kernel(kernel_pml4, boot_info->elf_hdr);

	/* Use the physical memory that 'bootstack' refers to as the kernel
	 * stack. The kernel stack grows down from virtual address KSTACK_TOP.
	 * Map 'bootstack' to [KSTACK_TOP - KSTACK_SIZE, KSTACK_TOP).
	 */
	if (DEBUG) {
		cprintf("\n\n-------------------------------------------------------------\n");
		cprintf("                 Mapping kernel stack\n");
		cprintf("-------------------------------------------------------------\n\n");
	}
	boot_map_region(kernel_pml4, (void *)(KSTACK_TOP - KSTACK_SIZE), KSTACK_SIZE, (uintptr_t)bootstack, flags);


	/* Map in the pages from the buddy allocator as RW-. */
	if (DEBUG) {
		cprintf("\n\n-------------------------------------------------------------\n");
		cprintf("             Mapping `struct page_info` array\n");
		cprintf("-------------------------------------------------------------\n\n");
	}
	boot_map_region(kernel_pml4, (void *)KPAGES, npages * sizeof(struct page_info), PADDR(pages), flags);

	physaddr_t va = KERNEL_VMA + KPAGES;
	physaddr_t pa = PADDR(pages);
	cprintf("mapping va = [%p, %p] to pa = [%p, %p]\n", va, va + npages * sizeof(struct page_info),
		pa, pa + npages * sizeof(struct page_info));

	buddy_migrate();

	return 0;
}

/*
 * Set up a four-level page table:
 * kernel_pml4 is its linear (virtual) address of the root
 *
 * This function only sets up the kernel part of the address space (i.e.
 * addresses >= USER_TOP). The user part of the address space will be set up
 * later.
 *
 * From USER_TOP to USER_LIM, the user is allowed to read but not write.
 * Above USER_LIM, the user cannot read or write.
 */
void mem_init(struct boot_info *boot_info)
{
	struct mmap_entry *entry;
	uintptr_t highest_addr = 0;
	uint32_t cr0;
	size_t i, n;

	/* Align the areas in the memory map. */
	align_boot_info(boot_info);

	/* Set up the buddy free lists. */
	for (i = 0; i < BUDDY_MAX_ORDER; ++i) {
		list_init(buddy_free_list + i);
	};

	/* Find the amount of pages to allocate structs for. */
	entry = (struct mmap_entry *)((physaddr_t)boot_info->mmap_addr);

	for (i = 0; i < boot_info->mmap_len; ++i, ++entry) {
		if (entry->type != MMAP_FREE)
			continue;

		highest_addr = entry->addr + entry->len;
	}

	npages = MIN(BOOT_MAP_LIM, highest_addr) / PAGE_SIZE;

	/*
	 * Allocate an array of npages 'struct page_info's and store it in 'pages'.
	 * The kernel uses this array to keep track of physical pages: for each
	 * physical page, there is a corresponding struct page_info in this array.
	 * 'npages' is the number of physical pages in memory.  Your code goes here.
	 */
	pages = boot_alloc(npages * sizeof *pages);

	page_init(boot_info);



	/* Perform the tests of lab 1. */
	lab1_check_mem(boot_info);

	/* Setup the initial PML4 for the kernel. */
	pml4_setup(boot_info);

	/* Enable the NX-bit. */
	write_msr(MSR_EFER,MSR_EFER_NXE); 

	/* Check the kernel PML4. */
	lab2_check_pml4();

	/* Load the kernel PML4. */
	load_pml4((struct page_table *)PADDR(kernel_pml4));

	/* Check the paging functions. */
	lab2_check_paging();

	/* Add the rest of the physical memory to the buddy allocator. */
	page_init_ext(boot_info);

	/* Check the buddy allocator. */
	lab2_check_buddy(boot_info);
}

void mem_init_mp(void)
{
	/* Set up kernel stacks for each CPU here. Make sure they have a guard
	 * page.
	 */
	/* LAB 6: your code here. */
	struct cpuinfo *cpu = cpus;
	int i;
	uint64_t kernel_va;
	uint64_t flags = (PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXEC);
	for(i = 1; i < ncpus; i++){
		cpu += i;
		if(cpu == boot_cpu)
			continue;
		kernel_va = KSTACK_TOP - (i * (KSTACK_SIZE + KSTACK_GAP));
		populate_region(kernel_pml4,(void *)(kernel_va - KSTACK_SIZE), KSTACK_SIZE, flags);
		cpu->cpu_tss.rsp[0] = kernel_va;
	}
}

/*
 * Initialize page structure and memory free list. After this is done, NEVER
 * use boot_alloc() again. After this function has been called to set up the
 * memory allocator, ONLY the buddy allocator should be used to allocate and
 * free physical memory.
 */
void page_init(struct boot_info *boot_info)
{
	struct page_info *page;
	struct mmap_entry *entry;
	uintptr_t pa, end;
	size_t i;
	uintptr_t index;

	/* Go through the array of struct page_info structs and:
	 *  1) call list_init() to initialize the linked list node.
	 *  2) set the reference count pp_ref to zero.
	 *  3) mark the page as in use by setting pp_free to zero.
	 *  4) set the order pp_order to zero.
	 */

	/*
	 *	Initializing the list with the nodes of the free physical 
	 *	pages and set the values of the page to 0
	 */
	for (i = 0; i < npages; ++i) {
		/* LAB 1: your code here. */

        page = pages + i;

        list_init(&page->pp_node);
        list_init(&page->swap_node);

        page->pp_ref = 0;
        page->pp_free = 0;
        page->pp_order = 0;
	}

	entry = (struct mmap_entry *)KADDR(boot_info->mmap_addr);
	end = PADDR(boot_alloc(0));

	/* Go through the entries in the memory map:
	 *  1) Ignore the entry if the region is not free memory.
	 *  2) Iterate through the pages in the region.
	 *  3) If the physical address is above BOOT_MAP_LIM, ignore.
	 *  4) Hand the page to the buddy allocator by calling page_free() if
	 *     the page is not reserved.
	 *
	 * What memory is reserved?
	 *  - Address 0 contains the IVT and BIOS data.
	 *  - boot_info->elf_hdr points to the ELF header.
	 *  - Any address in [KERNEL_LMA, end) is part of the kernel.
	 */

	/*
		
	 *	We are checking if the memory is free, then we take the entry point
	 *	and we calculate the corresponding ending point. We are gonna use
	 *	these two variables to go through all the pages and do the needed checks
	 *	and in the end we are gonna execute the page_free()
	 */
	for (i = 0; i < boot_info->mmap_len; ++i, ++entry) {
		if(entry->type == MMAP_FREE){
			uint64_t p_start = entry->addr;
			uint64_t p_end = p_start + entry->len;

			for(uintptr_t j = p_start; j < p_end; j+= PAGE_SIZE){
				if ((j < BOOT_MAP_LIM) && (j != 0) && j != PAGE_ADDR(PADDR(boot_info)) 
				&& ((void *) j != boot_info->elf_hdr) 
				&& (j < KERNEL_LMA || j >= end)) {
					index = PAGE_INDEX(j);
					page = pages + index;
					page_free(page);
					//page_free(pages+j/PAGE_SIZE);
				}
			}
		}
	}
}

/* Extend the buddy allocator by initializing the page structure and memory
 * free list for the remaining available memory.
 */
void page_init_ext(struct boot_info *boot_info)
{
	struct page_info *page;
	struct mmap_entry *entry;
	uintptr_t pa, end;
	size_t i;
	int ret;

	entry = (struct mmap_entry *)KADDR(boot_info->mmap_addr);
	end = PADDR(boot_alloc(0));

	/* Go through the entries in the memory map:
	 *  1) Ignore the entry if the region is not free memory.
	 *  2) Iterate through the pages in the region.
	 *  3) If the physical address is below BOOT_MAP_LIM, ignore.
	 *  4) Hand the page to the buddy allocator by calling page_free().
	 */


	uint64_t flags = (PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXEC);
	uintptr_t index;
	for (i = 0; i < boot_info->mmap_len; ++i, ++entry) {
		if(entry->type == MMAP_FREE){
			uint64_t p_start = entry->addr;
			uint64_t p_end = p_start + entry->len;

			for(uintptr_t j = p_start; j < p_end; j+= PAGE_SIZE){
				if (0) {
					cprintf("mapping: va = [%p, %p] -- BOOT_MAP_LIM: %p\n", 
						(KERNEL_VMA + j), (KERNEL_VMA + j) + 512 * PAGE_SIZE,
						KERNEL_VMA + BOOT_MAP_LIM);
				}
                if(j < BOOT_MAP_LIM) 
					continue;
				index = PAGE_INDEX(j);
				page = pages + index;

				if(index >= npages) {
					// We have run out of page_info structs, so create new ones
					if(buddy_map_chunk(kernel_pml4, index) < 0)
						panic("No pages remaining");

					// Map the 512 new pages starting from KERNEL_VMA where we did the previous mapping
					boot_map_region(kernel_pml4, (void *)KERNEL_VMA + j, 512 * PAGE_SIZE, j ,flags);
					if (1) {
						cprintf("mapping: va = [%p, %p] to pa = [%p, %p]\n", 
							(KERNEL_VMA + j), (KERNEL_VMA + j) + 512 * PAGE_SIZE, 
							j, j + 512 * PAGE_SIZE);
					} else {
						cprintf("\tto pa = [%p, %p]\n", 
							j, j + 512 * PAGE_SIZE);
					}
				}
				page_free(page);
            }
		}
	}
}
