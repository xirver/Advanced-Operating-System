#include <task.h>
#include <vma.h>

#include <kernel/mem.h>
#include <kernel/vma.h>
#include <include/list.h>
#include <kernel/sched/task.h>

/* Removes the given VMA from the given task. */
void remove_vma(struct task *task, struct vma *vma)
{
	if (!task || !vma) {
		return;
	}
		
	rb_remove(&task->task_rb, &vma->vm_rb);
	rb_node_init(&vma->vm_rb);
	list_del(&vma->vm_mmap);
}

/* Frees all the VMAs for the given task. */
void free_vmas(struct task *task)
{
	struct vma *vma;
	struct list *node, *head;

	head = &task->task_mmap;

	node = list_head(head);
	while (node) {
		vma = container_of(node, struct vma, vm_mmap);
		node = list_next(head, node);
		remove_vma(task, vma);
	}
}

/* Splits the VMA into the address range [base, base + size) and removes the
 * resulting VMA and any physical pages that back the VMA.
 */
int do_remove_vma(struct task *task, void *base, size_t size, struct vma *vma,
	void *udata)
{
	struct vma *vma_to_remove;
	void *start, *end;

	void *base_aligned, *end_aligned;
	size_t size_aligned;

	base_aligned = ROUNDDOWN(base, PAGE_SIZE);
	size_aligned = ROUNDUP(size, PAGE_SIZE);
	end_aligned = base_aligned + size_aligned;

	assert(base_aligned >= vma->vm_base);
	assert(end_aligned <= vma->vm_end);

	vma_to_remove = split_vmas(task, vma, base_aligned, size_aligned);
	if (!vma_to_remove) {
		return -1;
	}

	unmap_page_range(task->task_pml4, base_aligned, size_aligned);
	remove_vma(task, vma_to_remove);
	return 0;
}

/* Removes the VMAs and any physical pages backing those VMAs for the given
 * address range [base, base + size).
 */
int remove_vma_range(struct task *task, void *base, size_t size)
{
	return walk_vma_range(task, base, size, do_remove_vma, NULL);
}

/* Removes any non-dirty physical pages for the given address range
 * [base, base + size) within the VMA.
 */
int do_unmap_vma(struct task *task, void *base, size_t size, struct vma *vma,
	void *udata)
{
	struct page_info *page;
	void *total_size = base + size;
	physaddr_t *entry;

	for (; base < total_size; base += PAGE_SIZE) {
		page = page_lookup(task->task_pml4, base, &entry);
		if (!page)
			return -1;
		if(!(*entry & PAGE_DIRTY)){
			unmap_page_range(task->task_pml4, base, PAGE_SIZE);
		}
	}

	return 0;
}

/* Removes any non-dirty physical pages within the address range
 * [base, base + size).
 */
int unmap_vma_range(struct task *task, void *base, size_t size)
{
	return walk_vma_range(task, base, size, do_unmap_vma, NULL);
}

