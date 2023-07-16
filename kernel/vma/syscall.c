#include <types.h>
#include <cpu.h>

#include <kernel/acpi.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/vma.h>

#include <lib.h>

static void print_vma_flags(int vma_flags)
{
	cprintf("\t[%c%c%c] \n",
		(vma_flags & VM_READ) ? 'r' : '-',
		(vma_flags & VM_WRITE) ? 'w' : '-',
		(vma_flags & VM_EXEC) ? 'x' : '-');
}

int sys_mquery(struct vma_info *info, void *addr)
{
	struct vma *vma;
	struct list *node;
	physaddr_t *entry;

	/* Check if the user has read/write access to the info struct. */
	assert_user_mem(cur_task, info, sizeof *info, PAGE_USER | PAGE_WRITE);

	/* Do not leak information about the kernel space. */
	if (addr >= (void *)USER_LIM) {
		return -1;
	}

	/* Clear the info struct. */
	memset(info, 0, sizeof *info);

	/* Find the VMA with an end address that is greater than the requested
	 * address, but also the closest to the requested address.
	 */
	vma = find_vma(NULL, NULL, &cur_task->task_rb, addr);


	if (!vma) {
		/* If there is no such VMA, it means the address is greater
		 * than the address of any VMA in the address space, i.e. the
		 * user is requesting the free gap at the end of the address
		 * space. The base address of this free gap is the end address
		 * of the highest VMA and the end address is simply USER_LIM.
		 */
		node = list_tail(&cur_task->task_mmap);

		info->vm_end = (void *)USER_LIM;

		if (!node) {
			return 0;
		}

		vma = container_of(node, struct vma, vm_mmap);
		info->vm_base = vma->vm_end;

		return 0;
	}

	if (addr < vma->vm_base) {
		/* The address lies outside the found VMA. This means the user
		 * is requesting the free gap between two VMAs. The base
		 * address of the free gap is the end address of the previous
		 * VMA. The end address of the free gap is the base address of
		 * the VMA that we found.
		 */
		node = list_prev(&cur_task->task_mmap, &vma->vm_mmap);

		info->vm_end = vma->vm_base;

		if (!node) {
			return 0;
		}

		vma = container_of(node, struct vma, vm_mmap);
		info->vm_base = vma->vm_end;

		return 0;
	}

	/* The requested address actually lies within a VMA. Copy the
	 * information.
	 */
	strncpy(info->vm_name, vma->vm_name, 64);
	info->vm_base = vma->vm_base;
	info->vm_end = vma->vm_end;
	info->vm_prot = vma->vm_flags;
	info->vm_type = vma->vm_src ? VMA_EXECUTABLE : VMA_ANONYMOUS;

	/* Check if the address is backed by a physical page. */
	if (page_lookup(cur_task->task_pml4, addr, &entry)) {
		info->vm_mapped = (*entry & PAGE_HUGE) ? VM_2M_PAGE : VM_4K_PAGE;
	}

	return 0;
}

int check_permissions(void *addr, size_t len, int prot, int flags)
{
	if (!addr && flags & MAP_FIXED)
		return -1;

	// User trying to map in kernel space
	if((uint64_t)(addr + len) > USER_LIM){
		return -1;
	}

	if (!prot)
		return 0;

	// Can only write/exec if we can also read
	if ((prot & PROT_WRITE) || (prot & PROT_EXEC)) {
		if (!(prot & PROT_READ))
			return -1;
	}

	return 0;
}

void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd,
	uintptr_t offset)
{
	struct vma *vma;
	int ret;

	if (check_permissions(addr, len, prot, flags) < 0)
		return MAP_FAILED;
	
	// Only allow these flags
	if((flags & ~(MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED | MAP_POPULATE)) != 0) {
		return MAP_FAILED;
	}

	// MAP_FIXED: remove any previous mappings
	if(flags & MAP_FIXED){
		vma = task_find_vma(cur_task, addr);
		if(vma){
			ret = remove_vma_range(cur_task, vma->vm_base, vma->vm_end - vma->vm_base);
			if (ret < 0)
				return MAP_FAILED;
		}
	}

	// Add the new VMA to the task
	vma = add_vma(cur_task, "user", addr, len, prot);
	if (!vma)
		return MAP_FAILED;

	// MAP_POPULATE: populate the new VMA
	if(flags & MAP_POPULATE) {
		ret = populate_vma_range(cur_task, vma->vm_base, vma->vm_end - vma->vm_base, flags);
		if (ret < 0)
			return MAP_FAILED;
	}

	merge_vmas(cur_task, vma);

	if (addr)
		return addr;
	
	return vma->vm_base;
}

void sys_munmap(void *addr, size_t len)
{
	remove_vma_range(cur_task, addr, len);
}

int sys_mprotect(void *addr, size_t len, int prot)
{
	if (check_permissions(addr, len, prot, 0) < 0)
		return -1;

	if (protect_vma_range(cur_task, addr, len, prot) < 0)
		return -1;

	uint64_t page_flags = convert_flags_from_vma_to_pages(prot);
	protect_region(cur_task->task_pml4, addr, len, page_flags | PAGE_USER);

	return 0;
}

int sys_madvise(void *addr, size_t len, int advise)
{
	struct vma *vma;
	uint64_t page_flags;

	if (check_permissions(addr, len, 0, 0) < 0)
		return -1;

	if(advise == MADV_DONTNEED){
		unmap_vma_range(cur_task, addr, len);
	}

	if(advise == MADV_WILLNEED){
		vma = task_find_vma(cur_task, addr);
		if (!vma)
			return -1;

		page_flags = convert_flags_from_vma_to_pages(vma->vm_flags);
		populate_region(cur_task->task_pml4, addr, len, page_flags | PAGE_USER);
	}

	return 0;
}

