#include <types.h>
#include <list.h>
#include <paging.h>
#include <spinlock.h>
#include <string.h>
#include <cpu.h>
#include <kernel/sched/task.h>
#include <kernel/sched/kernel_thread.h>
#include <kernel/dev/oom.h>
#include <kernel/dev/swap.h>


#include <kernel/mem.h>

#define DEBUG 1 

char POISON[] = "&cC3ee48bKPP&jPkBWkFd!udF2%3Wae&Ra7Az8739b&d8UX*rr94oV%&3EM^BL#@3zgydFLiJT^L^X9!%8HW*@XnpkfH4YSYagXH";

extern struct swap_info swap;

/* Physical page metadata. */
size_t npages;
struct page_info *pages;

/*
 * List of free buddy chunks (often also referred to as buddy pages or simply
 * pages). Each order has a list containing all free buddy chunks of the
 * specific buddy order. Buddy orders go from 0 to BUDDY_MAX_ORDER - 1
 */
struct list buddy_free_list[BUDDY_MAX_ORDER];
struct list zero_list;

#ifndef USE_BIG_KERNEL_LOCK
/* Lock for the buddy allocator. */
struct spinlock buddy_lock = {
#ifdef DEBUG_SPINLOCK
	.name = "buddy_lock",
#endif
};
#endif

void lock_buddy(void) 
{
#ifndef USE_BIG_KERNEL_LOCK
	spin_lock(&buddy_lock);
#endif
}

void unlock_buddy(void) 
{
#ifndef USE_BIG_KERNEL_LOCK
	spin_unlock(&buddy_lock);
#endif
}


/* 
	Check if buddy free list is properly linked
*/
void debug_buddy_free_list() {

	size_t order;
	struct list *head, *node, *prev, *next;

	for (order = 0; order < BUDDY_MAX_ORDER; ++order) {
		head = &buddy_free_list[order];

		for (node = head->next; node && node != head; node = node->next) {
			prev = node->prev;
			assert(prev->next = node);
			next = node->next;
			assert(next->prev = node);
		}
	}
}

/*
	buddy_migrate() is causing bugs. This function is for debugging
*/
size_t debug_count_free_pages(size_t order)
{
	struct list *node;
	size_t nfree_pages = 0;

	if (order >= BUDDY_MAX_ORDER) {
		return 0;
	}

	node = &buddy_free_list[order];

	int i = 0;
	struct list *tmp;
	for (node = (buddy_free_list + order)->next; node != (buddy_free_list + order); node = node->next) {
		cprintf("\t%d\n", i);
		i+=1;
		++nfree_pages;
		cprintf("\t\tnode: %p\n", node);
		cprintf("\t\tnode->next: %p\n", node->next);
		cprintf("\t\tfine\n\n");

	}

	return nfree_pages;
}

void debug_show_buddy_info(void)
{
	struct page_info *page;
	struct list *node;
	size_t order;
	size_t nfree_pages;
	size_t nfree = 0;

	cprintf("Buddy allocator:\n");

	for (order = 0; order < BUDDY_MAX_ORDER; ++order) {
		nfree_pages = debug_count_free_pages(order);
		cprintf("  order #%u pages=%u\n", order, nfree_pages);
		nfree += nfree_pages * (1 << (order + 12));
	}

	cprintf("  free: %u kiB\n", nfree / 1024);
}

/* Counts the number of free pages for the given order.
 */
size_t count_free_pages(size_t order)
{
	struct list *node;
	size_t nfree_pages = 0;

	if (order >= BUDDY_MAX_ORDER) {
		return 0;
	}

	node = &buddy_free_list[order];

	list_foreach(buddy_free_list + order, node) {
		++nfree_pages;
	}

	return nfree_pages;
}

/* Shows the number of free pages in the buddy allocator as well as the amount
 * of free memory in kiB.
 *
 * Use this function to diagnose your buddy allocator.
 */
void show_buddy_info(void)
{
	struct page_info *page;
	struct list *node;
	size_t order;
	size_t nfree_pages;
	size_t nfree = 0;

	cprintf("Buddy allocator:\n");

	for (order = 0; order < BUDDY_MAX_ORDER; ++order) {
		nfree_pages = count_free_pages(order);

		cprintf("  order #%u pages=%u\n", order, nfree_pages);


		/*
		TESTING / DEBUG 
		*/

		if (0) {
			cprintf("\t");
			struct list *head = &buddy_free_list[order];
			cprintf("(%p) --> ", head);
			node = head;
			for (int i = 0; i < 10; ++i) {
				if (node == head)
					cprintf("(%p)", node);
				else {
					cprintf("0x%02x", *((char *) node));
				}
				cprintf(" | ");
				node = node->next;
			}
			cprintf("\n");
		}

		// Print physical addresses of page metadata in the free list
		if (1) {
			cprintf("\t");
			struct list *head = &buddy_free_list[order];
			cprintf("(%p) --> ", head);
			for (node = head->next; node && node != head; node = node->next) {
				cprintf("%p | ", node);
			}
			cprintf("\n");
		}

		// Print physical addresses of pages in the free list
		if (0) {
			cprintf("\t");
			struct list *head = &buddy_free_list[order];
			for (node = head->next; node && node != head; node = node->next) {
				cprintf("%p | ", page2pa(container_of(node, struct page_info, pp_node)));
			}
			cprintf("\n");
		}



		/*
		END TESTING / DEBUG 
		*/


		nfree += nfree_pages * (1 << (order + 12));
	}

	cprintf("  free: %u kiB\n", nfree / 1024);
}

/* Gets the total amount of free pages. */
size_t count_total_free_pages(void)
{
	struct page_info *page;
	struct list *node;
	size_t order;
	size_t nfree_pages;
	size_t nfree = 0;

	for (order = 0; order < BUDDY_MAX_ORDER; ++order) {
		nfree_pages = count_free_pages(order);
		nfree += nfree_pages * (1 << order);
	}

	return nfree;
}

/* Splits lhs into free pages until the order of the page is the requested
 * order req_order.
 *
 * The algorithm to split pages is as follows:
 *  - Given the page of order k, locate the page and its buddy at order k - 1.
 *  - Decrement the order of both the page and its buddy.
 *  - Mark the buddy page as free and add it to the free list.
 *  - Repeat until the page is of the requested order.
 *
 * Returns a page of the requested order.
 */
 struct page_info *buddy_split(struct page_info *lhs, size_t req_order)
{
	/* LAB 1: your code here. */
	
	//	if order is the same just return the page with the requested order
	if(lhs->pp_order == req_order){
		return lhs;
	}

	// block_size = 2^pp_order
	size_t block_size = 1 << lhs->pp_order;

	// Find buddy
	int page_index = page2pa(lhs) / PAGE_SIZE; 
	int buddy_index = page_index + block_size / 2;
	struct page_info *buddy;

	buddy = pages + buddy_index;
	
	list_del(&lhs->pp_node);

	// Decrement order of buddy page
	lhs->pp_order -= 1;
	buddy->pp_order = lhs->pp_order;

	// Mark buddy page to free
	buddy->pp_free = 1;
	
	// Add buddy page to the free list
	struct list *head = &buddy_free_list[lhs->pp_order];
	list_add(head, &lhs->pp_node);
	list_add(head, &buddy->pp_node);

	return buddy_split(lhs, req_order);
}

/* Merges the buddy of the page with the page if the buddy is free to form
 * larger and larger free pages until either the maximum order is reached or
 * no free buddy is found.
 *
 * The algorithm to merge pages is as follows:
 *  - Given the page of order k, locate the page with the lowest address
 *    and its buddy of order k.
 *  - Check if both the page and the buddy are free and whether the order
 *    matches.
 *  - Remove the page and its buddy from the free list.
 *  - Increment the order of the page.
 *  - Repeat until the maximum order has been reached or until the buddy is not
 *    free.
 *
 * Returns the largest merged free page possible.
 */
struct page_info *buddy_merge(struct page_info *page)
{
	/* LAB 1: your code here. */
	int i;
	int page_index, buddy_index;
	struct page_info *lhs, *buddy;
	size_t block_size = 1 << page->pp_order;
	
	page_index = page2pa(page) / PAGE_SIZE; 

	if (page->pp_order == BUDDY_MAX_ORDER - 1)
		return NULL; 

	// Add page to free list without merging if the list is empty
    struct list *head = &buddy_free_list[page->pp_order];
    if (list_is_empty(head)) {
		return page;
	}

	// Find buddy of our page

	// Determine if buddy is on the left or right side
	if (page_index % (block_size * 2) == 0) {
		// Buddy is on the right side
	 	buddy = &pages[page_index + block_size];
		lhs = page;
	} else {
		// Buddy is on the left side
	 	buddy = &pages[page_index - block_size];
		lhs = buddy;
	}
	buddy_index = PAGE_INDEX(page2pa(buddy));

	// Check if buddy is free and the same order as our page
	if (page->pp_free && buddy->pp_free && page->pp_order == buddy->pp_order) {
		// Remove page and buddy from lower order
		list_del(&buddy->pp_node);
		list_del(&page->pp_node);

		// Set rhs pages to not-free	
		if (buddy == lhs)
			page->pp_free = 0;
		else
			buddy->pp_free = 0;

		// Move new merged block to higher order
		lhs->pp_order += 1;
		page_index = PAGE_INDEX(page2pa(lhs));
		head = &buddy_free_list[lhs->pp_order];
		list_add(head, &lhs->pp_node);

		page = buddy_merge(lhs);

		return NULL;
	} 
	
	return page; 
}

/* Given the order req_order, attempts to find a page of that order or a larger
 * order in the free list. In case the order of the free page is larger than the
 * requested order, the page is split down to the requested order using
 * buddy_split().
 *
 * Returns a page of the requested order or NULL if no such page can be found.
 */
struct page_info *buddy_find(size_t req_order)
{
	struct page_info *page;
	struct list *head;
	
	//	if the list of the requested order is not empty pop and return it
	if(!list_is_empty(&buddy_free_list[req_order])) {
		page = (struct page_info *) list_pop(&buddy_free_list[req_order]);
		return page;
	}
	//	otherwise we are gonna go through all orders, pop the page of the valid order and return buddy
	for (size_t order = req_order + 1; order < BUDDY_MAX_ORDER; ++order) {
		head = &buddy_free_list[order];
		if (list_is_empty(head))
			continue;
		page = (struct page_info *) list_pop(&buddy_free_list[order]);
		return buddy_split(page, req_order);
	}

	return NULL;
}

/*
 * Allocates a physical page.
 *
 * if (alloc_flags & ALLOC_ZERO), fills the entire returned physical page with
 * '\0' bytes.
 * if (alloc_flags & ALLOC_HUGE), returns a huge physical 2M page.
 *
 * Beware: this function does NOT increment the reference count of the page -
 * this is the caller's responsibility.
 *
 * Returns NULL if out of free memory.
 *
 * Hint: use buddy_find() to find a free page of the right order.
 * Hint: use page2kva() and memset() to clear the page.
 */
struct page_info *page_alloc(int alloc_flags)
{
	/* LAB 1: your code here. */
	struct page_info *page;
	uint64_t addr;

	lock_buddy();

	// Find a page of order 0
	page = buddy_find(0);

	assert(page);

	if (!page) {
		unlock_buddy();
		return NULL;
	}

	assert(page->pp_free == 1);
		
	// Initialize to zero if the flag is set
	addr = (uint64_t) page2kva(page);
	if (alloc_flags & ALLOC_ZERO) {
		// Zero the page if not yet done
		if (!page->pp_zero)
			memset((void *) addr, '\0', PAGE_SIZE);
	}

	// Remove page from free list
	page->pp_free = 0;
	list_del(&page->pp_node);

	unlock_buddy();

	return page;
}


/*
 * Return a page to the free list.
 * (This function should only be called when pp->pp_ref reaches 0.)
 *
 * Hint: mark the page as free and use buddy_merge() to merge the free page
 * with its buddies before returning the page to the free list.
 */
void page_free(struct page_info *pp)
{
	struct list *head;
	int page_index;

	if (!(pp->pp_ref == 0))
		debug_print("(CPU %d) pp_ref: %d\n", this_cpu->cpu_id, pp->pp_ref);
    assert(pp->pp_ref == 0);

	lock_buddy();

	// Remove page node from page replacement list
	remove_swap_page(pp);

	// Check if we can merge the page
    pp->pp_free = 1;
	if (pp->pp_order != BUDDY_MAX_ORDER - 1)
		pp = buddy_merge(pp);

	// Add new page to free list if the merger did not already add it
	if (!pp) {
		unlock_buddy();
		return;
	}

	head = &buddy_free_list[pp->pp_order];
	page_index = page2pa(pp) / PAGE_SIZE; 
	list_add(head, &pp->pp_node);

	// Page has not been zeroed
	pp->pp_zero = 0;
	list_add(&zero_list, &pp->pp_zero_node);

	unlock_buddy();
}

/*
 * Decrement the reference count on a page,
 * freeing it if there are no more refs.
 */
void page_decref(struct page_info *pp)
{
	if (--pp->pp_ref == 0) {
		page_free(pp);
	}
}

static int in_page_range(void *p)
{
	return ((uintptr_t)pages <= (uintptr_t)p &&
	        (uintptr_t)p < (uintptr_t)(pages + npages));
}

static void *update_ptr(void *p)
{
	if (!in_page_range(p))
		return p;

	if ((long long) p == 0xffff80000011d580) {
		void *tmp = (void *)((uintptr_t)p + KPAGES - (uintptr_t)pages);
		cprintf("migrating: %p  -->  %p\n", p, tmp);
	}

	return (void *)((uintptr_t)p + KPAGES - (uintptr_t)pages);
}

void buddy_migrate(void)
{
	struct page_info *page;
	struct list *node;
	size_t i;

	for (i = 0; i < npages; ++i) {
		page = pages + i;
		node = &page->pp_node;

		node->next = update_ptr(node->next);
		node->prev = update_ptr(node->prev);
	}

	for (i = 0; i < BUDDY_MAX_ORDER; ++i) {
		node = buddy_free_list + i;

		node->next = update_ptr(node->next);
		node->prev = update_ptr(node->prev);
	}

	pages = (struct page_info *)KPAGES;
}

int buddy_map_chunk(struct page_table *pml4, size_t index)
{
	struct page_info *page, *base;
	void *end;
	size_t nblocks = (1 << (12 + BUDDY_MAX_ORDER - 1)) / PAGE_SIZE;
	size_t nalloc = ROUNDUP(nblocks * sizeof *page, PAGE_SIZE) / PAGE_SIZE;
	size_t i;

	// nblocks = 512: new struct page_info added to pages 
	// nalloc = 4: pages are required to store the structs

	index = ROUNDDOWN(index, nblocks);
	base = pages + index;

	for (i = 0; i < nalloc; ++i) {
		page = page_alloc(ALLOC_ZERO);

		if (!page) {
			return -1;
		}

		if (page_insert(pml4, page, (char *)base + i * PAGE_SIZE,
		    PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXEC) < 0) {
			return -1;
		}
	}

	for (i = 0; i < nblocks; ++i) {
		page = base + i;
		list_init(&page->pp_node);
		list_init(&page->swap_node);
	}

	npages = index + nblocks;

	return 0;
}
