#include <types.h>

#include <kernel/mem.h>
#include <kernel/vma.h>
#include <kernel/mem/map.h>

#define DEBUG 0

void print_vma_flags(int vma_flags)
{
	cprintf("\t[%c%c%c] \n",
		(vma_flags & VM_READ) ? 'r' : '-',
		(vma_flags & VM_WRITE) ? 'w' : '-',
		(vma_flags & VM_EXEC) ? 'x' : '-');
}

/* Check for appropriate permissions for accessing pages in the VMA. Access
 * privileges must not be higher than VMA privileges
 */
int check_vma_permissions(struct vma *vma, int vma_flags)
{
	if ((vma_flags & VM_READ) > (vma->vm_flags & VM_READ)|| 
		(vma_flags & VM_WRITE) > (vma->vm_flags & VM_WRITE) ||
		(vma_flags & VM_EXEC) > (vma->vm_flags & VM_EXEC)) {

		if (DEBUG) {
			cprintf("\n[do_populate_vma]: flags are not matching\n");
			cprintf("\tVMA flags:\n");
			print_vma_flags(vma->vm_flags);
			cprintf("\tFlags which we want to map:\n\n");
			print_vma_flags(vma_flags);
			cprintf("\n");
		}

		return -1;
	}

	return 0;
}

/* Checks the flags in udata against the flags of the VMA to check appropriate
 * permissions. If the permissions are all right, this function populates the
 * address range [base, base + size) with physical pages. If the VMA is backed
 * by an executable, the data is copied over. Then the protection of the
 * physical pages is adjusted to match the permissions of the VMA.
 */
int do_populate_vma(struct task *task, void *base, size_t size,
	struct vma *vma, void *udata)
{
	/*
	void *udata: contains vma flags, not page flags
	*/
	int vma_flags = *((int *) udata);
	uint64_t page_flags;

	if (check_vma_permissions(vma, vma_flags) < 0)
		return -1;

	/*
	Adjust base and end if [base, base+end) spans multiple vmas

		|		(base)	   |				| 		(base + size)	  |

	*/
	void *p_base, *p_end;
	void *end = base + size;
	size_t p_size;
	p_base = base > vma->vm_base ? base: vma->vm_base;
	p_end = end < vma->vm_end ? end : vma->vm_end;
	p_size = p_end - p_base;

	// Offset in bytes from start of source data
	size_t offset = p_base - vma->vm_base;
	void *src = vma->vm_src + offset;
	size_t len = vma->vm_len - offset < PAGE_SIZE ? vma->vm_len - offset : PAGE_SIZE;

	if (vma->vm_src) {
		assert(vma->vm_len);
		
		// Temporary flags to allow writing to the pages
		page_flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

		populate_region(task->task_pml4, p_base, p_size, page_flags);
		memset((void *) p_base, 0, p_size);

		// Copy the data
		memcpy((void *) p_base, src, len);

		// Set the actual flags - add PAGE_USER because this is a task
		page_flags = convert_flags_from_vma_to_pages(vma->vm_flags) | PAGE_USER;
		protect_region(task->task_pml4, p_base, p_size, page_flags);
	} 
	else {
		page_flags = convert_flags_from_vma_to_pages(vma->vm_flags) | PAGE_USER;
		populate_region(task->task_pml4, p_base, p_size, page_flags);
	}

	return 0;
}

/* Populates the VMAs for the given address range [base, base + size) by
 * backing the VMAs with physical pages.
 */
int populate_vma_range(struct task *task, void *base, size_t size, int flags)
{
	return walk_vma_range(task, base, size, do_populate_vma, &flags);
}

