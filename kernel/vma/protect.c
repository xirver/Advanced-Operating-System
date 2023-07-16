#include <types.h>

#include <kernel/mem.h>
#include <kernel/vma.h>

/* Changes the protection flags of the given VMA. Does nothing if the flags
 * would remain the same. Splits up the VMA into the address range
 * [base, base + size) and changes the protection of the physical pages backing
 * the VMA. Then attempts to merge the VMAs in case the protection became the
 * same as that of any of the adjacent VMAs.
 */

int do_protect_vma(struct task *task, void *base, size_t size, struct vma *vma,
	void *udata)
{
	struct vma *s_vma;
	uint64_t page_flags;
	
	// If protection flags are equal do nothing
	if(vma->vm_flags == *(int *)udata){
		return 0;
	}

	// Split VMA so we change the flags on one part
	s_vma = split_vma(task, vma, base);
	if(!s_vma){
		return -1;
	}

	// Update protection flags of the split VMA
	s_vma->vm_flags = *(int *)udata;

	// Change protection of physical pages
	// --> Added after assignment feedback - NOT TESTED
	page_flags = convert_flags_from_vma_to_pages(s_vma->vm_flags);
	protect_region(task->task_pml4, base, size, page_flags);

	merge_vmas(task, s_vma);

	return 0;
}

/* Changes the protection flags of the VMAs for the given address range
 * [base, base + size).
 */
int protect_vma_range(struct task *task, void *base, size_t size, int flags)
{
	return walk_vma_range(task, base, size, do_protect_vma, &flags);
}

