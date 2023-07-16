#include <types.h>
#include <cpu.h>
#include <list.h>
#include <stdio.h>
#include <vma.h>

#include <kernel/mem/buddy.h>
#include <kernel/mem/walk.h>
#include <kernel/sched/task.h>
#include <kernel/dev/swap.h>
#include <kernel/dev/oom.h>
#include <kernel/dev/disk.h>
#include <kernel/dev/rmap.h>
#include <kernel/sched/kernel_thread.h>

#define DEBUG 1

extern struct spinlock console_lock;

/*
 * Given a page frame, find all PTE entries that map to this page
 * and perform an operation there as defined in the walker
 */
void rmap_walk(struct page_info *page, struct page_walker *walker)
{
	struct rmap *rmap;
    struct list *vma_node;
    struct task *task;
    struct vma *vma;

    rmap = page->rmap;
    assert(rmap);

	debug_print("(CPU %d) Starting VMA loop of rmap\n", this_cpu->cpu_id);

	spin_lock(&rmap->lock);

	assert(!list_is_empty(&rmap->vmas));

	// Loop through VMAs in rmap
	debug_print("(CPU %d) Rmap VMAs:\n", this_cpu->cpu_id);
	list_foreach(&rmap->vmas, vma_node) {
		vma = container_of(vma_node, struct vma, rmap_node);
		assert(vma);

        task = vma->task;
		assert(task);

		if (walk_page_range(task->task_pml4, vma->vm_base, vma->vm_end, walker) < 0){
			continue;
		}
	}
	spin_unlock(&rmap->lock);
}