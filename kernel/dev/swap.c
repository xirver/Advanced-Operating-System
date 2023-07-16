#include <types.h>
#include <cpu.h>
#include <list.h>
#include <stdio.h>
#include <vma.h>

#include <kernel/mem/buddy.h>
#include <kernel/mem/walk.h>
#include <kernel/sched/task.h>
#include <kernel/dev/swap.h>
#include <kernel/dev/swap_util.h>
#include <kernel/dev/oom.h>
#include <kernel/dev/disk.h>
#include <kernel/dev/rmap.h>
#include <kernel/sched/kernel_thread.h>

#define DEBUG 1

#define SWAP_BLOCK 1000

extern pid_t pid_max;

extern struct spinlock console_lock;

struct swap_info swap;

/*
page replacement algorithm are needed to decide which page needed to be 
replaced when new page comes in. Whenever a new page is referred and not present
in memory, page fault occurs and Operating System replaces one of the existing pages 
with newly needed page. Different page replacement algorithms suggest different ways 
to decide which page to replace. The target for all algorithms is to reduce number of page faults. 
*/

/*
    Function for replacing pages

    -queue of faulting pages, replace the head each time
    -queue must be circular for clock and it moves the head
    -when we visit the head we check the referenc or access bit to be sure that the page is or it is not been accessed since the initial page fault.
        IF bit is 0 then replace page, IF is 1 then set bit to 0 and move the page to tail.
*/
static int check_access_flag(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
	struct clock_info *info = walker->udata;

	if ((*entry & PAGE_PRESENT) && PAGE_ADDR(*entry) == info->pa && (*entry & PAGE_ACCESSED)) {
		info->accessed = 1;

		// Disable PAGE_ACCESSED bit
		*entry = *entry & (~PAGE_ACCESSED);
	}

	return 0;
}

struct page_info *check_clock(void)
{
	struct list *node, *rmap, *rmap_node;
	struct task *task;
	struct vma *vma;
	struct page_info *page;
	struct clock_info info;

	struct page_walker walker = {
		.pte_callback = check_access_flag,
		.udata = &info,
	};

	node = list_pop_tail(&swap.pages);
	page = container_of(node, struct page_info, swap_node);

	info.accessed = 0;

    if (!page) {
        return NULL;
    }

	info.pa = page2pa(page);
    debug_print("(CPU %d) Walking pages to find PAGE_ACCESSED bit value\n", this_cpu->cpu_id);
    rmap_walk(page, &walker);

	// Page has been accessed so move it to head of swap list
	if (info.accessed) {
		mru_swap_page(page);
		return NULL;
	}

	return page;
}

// Get the page for swap out
struct page_info *get_page(void) 
{
    struct list *node;
    struct page_info *page;

    spin_lock(&swap.lock);

    if (0) {
        cprintf("waiting for console lock\n");
        spin_lock(&console_lock);

        struct list *head = &swap.pages;
        node = head->next;
        cprintf("\thead  : %p\n", head);
        for (int i = 0; i < 20; ++i) {
            cprintf("\tnode %d: %p\n", i, node);
            node = node->next;
        }

        spin_unlock(&console_lock);
    }

    if (list_is_empty(&swap.pages)) {
        spin_unlock(&swap.lock);
        return NULL;
	}

    page = NULL;
    while (!page) {
        page = check_clock();
    }

    spin_unlock(&swap.lock);

    return page;
}

static int update_pte_swap_in(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
    struct rmap_info *info = walker->udata;

    // Enable PAGE_PRESENT bit and insert the physical page address
    if (PAGE_ADDR(*entry) == page2pa(info->page)) {
        assert(*entry & PAGE_PRESENT);

        // Enable PAGE_PRESENT bit
        *entry += PAGE_PRESENT;

        // Clear out disk address
        *entry = *entry & PAGE_MASK;

        // Write the physical address
        *entry += info->pa;
        info->page->pp_ref--;
    }

    return 0;
}

void update_rmap_ptes_swap_in(struct page_info *page, physaddr_t pa)
{
    struct list *node;
    
	struct rmap_info info = {
        .page = page,
        .pa = pa,
    };

	struct page_walker walker = {
		.pte_callback = update_pte_swap_in,
		.udata = &info,
	};

    debug_print("(CPU %d) Changing PTE value from disk address to physical page\n", this_cpu->cpu_id);
    rmap_walk(page, &walker);

}

int swap_in(physaddr_t *entry)
{
    struct disk *disk = disks[1];
    struct disk_stat stat;
    struct page_info *swap_page;
    uint64_t disk_addr;   
    int ret;

    debug_print("(CPU %d) Swapping in page\n", this_cpu->cpu_id);

    disk->ops->stat(disk, &stat);

    spin_lock(&swap.lock);

    swap_page = page_alloc(ALLOC_ZERO);

    if(!swap_page){
        spin_unlock(&swap.lock);
        return -1;
    }

    if(!disk->ops->poll(disk)){
        spin_unlock(&swap.lock);
        return -EAGAIN;
    }

    disk_addr = PAGE_ADDR(*entry);

    ret = disk->ops->read(disk, page2kva(swap_page), PAGE_SIZE, disk_addr);
    if (ret < 0)
        spin_unlock(&swap.lock);
        return -1;

    // Update PTEs
    update_rmap_ptes_swap_in(swap_page, page2pa(swap_page));

    spin_unlock(&swap.lock);

    return 0;
}

/* For now just write incrementally on disk to keep it simple */
physaddr_t free_disk_addr = 0;

physaddr_t get_free_disk_addr(void) 
{
    physaddr_t addr = free_disk_addr;

    free_disk_addr += PAGE_SIZE;

    return addr;
}


static int update_pte_swap_out(physaddr_t *entry, uintptr_t base, uintptr_t end,
    struct page_walker *walker)
{
    struct rmap_info *info = walker->udata;

    // Disable PAGE_PRESENT bit and insert the disk address
    if (PAGE_ADDR(*entry) == page2pa(info->page)) {
        assert(*entry & PAGE_PRESENT);

        // Disable PAGE_PRESENT bit
        *entry = *entry & (~PAGE_PRESENT);

        // Clear out physical page address
        *entry = *entry & PAGE_MASK;

        // Write the disk address
        *entry += info->disk_addr;
        info->page->pp_ref--;
    }

    return 0;
}

/*
 * Update all PTEs to point to the address on disk
 */
void update_rmap_ptes_swap_out(struct page_info *page, physaddr_t disk_addr)
{
    struct list *node;
    
	struct rmap_info info = {
        .page = page,
        .disk_addr = disk_addr,
    };

	struct page_walker walker = {
		.pte_callback = update_pte_swap_out,
		.udata = &info,
	};

    debug_print("(CPU %d) Changing PTE value from physical page to disk address\n", this_cpu->cpu_id);
    rmap_walk(page, &walker);

}

int swap_out(void)
{
    struct disk *disk;
    struct page_info *swap_page;
    struct disk_stat *stat;
    physaddr_t disk_addr;
    int ret;

    disk = disks[1];

    if (!disk->ops->poll(disk)) {
        return -1;
    }

    swap_page = get_page();
    if (!swap_page) {
        return -1;
    }


    disk_addr = get_free_disk_addr();

    ret = disk->ops->write(disk, page2kva(swap_page), PAGE_SIZE, disk_addr);
    if (ret < 0)
        return -1;

    // Update all PTEs from the rmap
    update_rmap_ptes_swap_out(swap_page, disk_addr);
    
    page_free(swap_page);
    
    return 0;
}

void initialize_swap_list(void) {
    
    list_init(&swap.pages);
    spin_init(&swap.lock, "swap_lock");
}

void yield_swap(void)
{
    cur_task->task_frame.rip = (uint64_t) &swap_thread;
    cur_task->task_frame.rsp = KERNEL_STACK_TOP;
    sched_yield();

}

void swap_thread(void) 
{
    struct task *task;
    uint64_t free_memory;

    // If a task is already dying, don't do anything. 
    // Killing that task will free memory
    for (pid_t pid = 1; pid < pid_max; pid++) {
        task = pid2task(pid, 0);
        if (!task) {
            continue;
        }

        if (task->task_status == TASK_DYING) {
            yield_swap();
        }
    }

    free_memory = get_total_free_memory();
    debug_print("(CPU %d) Free memory: %d / %d\n", this_cpu->cpu_id, free_memory, MEMORY_THRESHOLD);
    if (free_memory < MEMORY_THRESHOLD) {
        // Under memory pressure: kill the task with highest OOM score
        debug_print("(CPU %d) Starting swap out\n", this_cpu->cpu_id);
        for (int i = 0; i < SWAP_BLOCK; ++i) {
            // If disk is busy, don't block - switch task
            if (swap_out() < 0) 
                yield_swap();
        }
    }

    yield_swap();
}

