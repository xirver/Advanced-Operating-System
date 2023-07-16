#include <task.h>
#include <vma.h>

#include <kernel/vma.h>
#include <kernel/mem.h>

/* Given a task and a VMA, this function splits the VMA at the given address
 * by setting the end address of original VMA to the given address and by
 * adding a new VMA with the given address as base.
 */
struct vma *split_vma(struct task *task, struct vma *lhs, void *addr)
{
	/* LAB 4: your code here. */
	struct vma *new_vma;
	size_t size = lhs->vm_end - addr;

	// No splitting is necessary
	if (lhs->vm_base == addr)
		return lhs;

	// Splitting not possible
	if(lhs->vm_end == addr)
		return NULL;
	
	lhs->vm_end = addr;

	new_vma = add_vma(task, lhs->vm_name, addr, size, lhs->vm_flags);
	if (!new_vma)
		return NULL;

	return new_vma;
}

/* Given a task and a VMA, this function first splits the VMA into a left-hand
 * and right-hand side at address base. Then this function splits the
 * right-hand side or the original VMA, if no split happened, into a left-hand
 * and a right-hand side. This function finally returns the right-hand side of
 * the first split or the original VMA.
 */
struct vma *split_vmas(struct task *task, struct vma *vma, void *base, size_t size)
{
	struct vma *lhs, *mid, *rhs;
	void *rhs_addr = base + size;

	lhs = vma;

	mid = split_vma(task, lhs, base);
	if (!mid)
		return NULL;

	rhs = split_vma(task, mid, rhs_addr);

	return mid;
}

