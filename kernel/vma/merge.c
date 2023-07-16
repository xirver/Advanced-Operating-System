#include <task.h>
#include <vma.h>

#include <kernel/vma.h>

/* Given a task and two VMAs, checks if the VMAs are adjacent and compatible
 * for merging. If they are, then the VMAs are merged by removing the
 * right-hand side and extending the left-hand side by setting the end address
 * of the left-hand side to the end address of the right-hand side.
 */
struct vma *merge_vma(struct task *task, struct vma *lhs, struct vma *rhs)
{
	// Only merge if flags are equal and the VMAs are neighbors
	if(!(lhs->vm_flags == rhs->vm_flags && lhs->vm_end == rhs->vm_base && lhs->vm_name == rhs->vm_name)){
		return NULL;
	}

	// Extend left hand side to the right end side
	lhs->vm_end = rhs->vm_end;

	// Remove rhs VMA from the given task
	remove_vma(task, rhs);

	return lhs;
}

/* Given a task and a VMA, this function attempts to merge the given VMA with
 * the previous and the next VMA. Returns the merged VMA or the original VMA if
 * the VMAs could not be merged.
 */
struct vma *merge_vmas(struct task *task, struct vma *vma)
{
	struct list *prev_list, *next_list;
	struct vma *new_vma, *prev_vma, *next_vma;

	// Try to merge (prev) with (vma)
	prev_list = list_prev(&task->task_mmap, &vma->vm_mmap);
	if (prev_list) {
		prev_vma = container_of(prev_list, struct vma, vm_mmap);
		new_vma = merge_vma(task, prev_vma, vma);
		if (new_vma) 
			vma = new_vma;
	}

	// Try to merge (prev + vma) with (next)
	next_list = list_next(&task->task_mmap, &vma->vm_mmap);
	if (next_list){
		next_vma = container_of(next_list, struct vma, vm_mmap);
		new_vma = merge_vma(task, vma, next_vma);
		if (new_vma) 
			vma = new_vma;
	}

	return vma;
}

