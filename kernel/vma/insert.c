#include <types.h>

#include <kernel/mem.h>
#include <kernel/vma.h>
#include <kernel/dev/rmap.h>
#include <lib.h>

/* Inserts the given VMA into the red-black tree of the given task. First tries
 * to find a VMA for the end address of the given end address. If there is
 * already a VMA that overlaps, this function returns -1. Then the VMA is
 * inserted into the red-black tree and added to the sorted linked list of
 * VMAs.
 */

int insert_vma(struct task *task, struct vma *vma)
{
	struct rb_node *node, *parent = NULL;
        struct vma *vma_tmp = NULL;
	int dir;


	node = task->task_rb.root;

	while (node) {
		vma_tmp = container_of(node, struct vma, vm_rb);
		parent = node;
		dir = (vma->vm_base >= vma_tmp->vm_end);

		if (!dir) {
		    /* If dir == 0 check if we don't overlap vma_tmp */
                    if (vma->vm_end > vma_tmp->vm_base){
						cprintf("[insert_vma]: Error - vma overlapping\n");
                        return -1;
                    }
		}

		node = node->child[dir];
	}

        if (!parent){
            task->task_rb.root = &vma->vm_rb;
        } else {
            parent->child[dir] = &vma->vm_rb;
            vma->vm_rb.parent = parent;
        }

        /* Balance the RED-BLACK tree after VMA insertion */
	if (rb_balance(&task->task_rb, &vma->vm_rb) < 0) {
		cprintf("Error: rb_balance failed\n");
		return -1;
	}

	if (!parent) {
		list_insert_after(&task->task_mmap, &vma->vm_mmap);
	} else {
                assert(vma_tmp);
		if (dir) {
			list_insert_after(&vma_tmp->vm_mmap, &vma->vm_mmap);
		} else {
			list_insert_before(&vma_tmp->vm_mmap, &vma->vm_mmap);
		}
	}

	return 0;
}

/* Allocates and adds a new VMA for the given task.
 *
 * This function first allocates a new VMA. Then it copies over the given
 * information. The VMA is then inserted into the red-black tree and linked
 * list. Finally, this functions attempts to merge the VMA with the adjacent
 * VMAs.
 *
 * Returns the new VMA if it could be added, NULL otherwise.
 */
struct vma *add_executable_vma(struct task *task, char *name, void *addr,
	size_t size, int flags, void *src, size_t len)
{
	/* LAB 4: your code here. */

	struct vma *vma;

	vma = add_vma(task, name, addr, size, flags);
	if (!vma)
		return NULL;

	assert(vma->vm_base == ROUNDDOWN(addr, PAGE_SIZE));
	assert(vma->vm_end == ROUNDUP(addr + size, PAGE_SIZE));

	// Displace the src by the same displacement as addr within the page
	uint64_t offset = ((uint64_t) addr % PAGE_SIZE);
	vma->vm_src = src - offset;
	vma->vm_len = len + offset;

	return merge_vmas(task, vma);
}

/* 
 * A simplified wrapper to add anonymous VMAs, i.e. VMAs not backed by an
 * executable.
 */
struct vma *add_anonymous_vma(struct task *task, char *name, void *addr,
	size_t size, int flags)
{
	return add_executable_vma(task, name, addr, size, flags, NULL, 0);
}

/*
 * Create a new VMA and insert it into the rb tree and list of VMA's for
 * the given task
 */
struct vma *create_vma(struct task *task, char *name, void *addr, size_t size,
	int flags)
{
	struct rmap *rmap;
	struct vma *vma;

	assert(task);

	// Create new vma structure
	vma = kmalloc(sizeof (*vma));
	if(!vma) {
		cprintf("Error: kmalloc failed\n");
		return NULL;
	}

	// Initialize vma linked list node and rb tree node
	list_init(&vma->vm_mmap);
	rb_node_init(&vma->vm_rb);


	// Set vma values - this determines its place in the rb tree
	vma->vm_name = name;
	vma->vm_base = ROUNDDOWN(addr, PAGE_SIZE);
	vma->vm_end = ROUNDUP(addr + size, PAGE_SIZE);
	vma->vm_flags = flags;
	vma->task = task;

	// Setup reverse mapping
	rmap = kmalloc(sizeof (struct rmap));
	list_init(&rmap->vmas);
	spin_init(&rmap->lock, "rmap");
	vma->rmap = rmap;
	list_add(&rmap->vmas, &vma->rmap_node);

	if(insert_vma(task, vma) < 0) {
		cprintf("Error: insert_vma failed\n");
		kfree(vma);
		return NULL;
	}

	return vma;
}


/*
 * This is basically the same as sys_mquery(). Only the assert_user_mem()
 * check has been removed because we can call this function from the 
 * kernel. This function finds a VMA at the given address.
 */
int find_free_vma(struct task *task, struct vma_info *info, void *addr)
{
	struct vma *vma;
	struct list *node;
	physaddr_t *entry;

	/* Do not leak information about the kernel space. */
	if (addr >= (void *)USER_LIM) {
		return -1;
	}

	/* Clear the info struct. */
	memset(info, 0, sizeof *info);

	/* Find the VMA with an end address that is greater than the requested
	 * address, but also the closest to the requested address.
	 */
	vma = find_vma(NULL, NULL, &task->task_rb, addr);

	if (!vma) {
		/* If there is no such VMA, it means the address is greater
		 * than the address of any VMA in the address space, i.e. the
		 * user is requesting the free gap at the end of the address
		 * space. The base address of this free gap is the end address
		 * of the highest VMA and the end address is simply USER_LIM.
		 */
		node = list_tail(&task->task_mmap);

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
		node = list_prev(&task->task_mmap, &vma->vm_mmap);

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
	if (page_lookup(task->task_pml4, addr, &entry)) {
		info->vm_mapped = (*entry & PAGE_HUGE) ? VM_2M_PAGE : VM_4K_PAGE;
	}

	return 0;
}

/* Allocates and adds a new VMA to the requested address or tries to find a
 * suitable free space that is sufficiently large to host the new VMA. If the
 * address is NULL, this function scans the address space from the end to the
 * beginning for such a space. If an address is given, this function scans the
 * address space from the given address to the beginning and then scans from
 * the end to the given address for such a space.
 *
 * Returns the VMA if it could be added. NULL otherwise.
 */
struct vma *add_vma(struct task *task, char *name, void *addr, size_t size,
	int flags)
{
	struct vma_info info;
	void *p;

	// If no address is given, scan from end to beginning
	if (!addr) {
		addr = (void *) USER_LIM;
	}

	// Search from the given address to the beginning
	p = addr;
	while (p > 0) {
		if (find_free_vma(task, &info, p) < 0) {
			break;
		}

		if (info.vm_type == VMA_FREE)
			return create_vma(task, name, p, size, flags);

		p = info.vm_base - 1;
	}

	// Search from the end to the given address
	p = ROUNDDOWN((void *) USER_LIM - size - 1, PAGE_SIZE);
	while (p >= 0) {
		if (find_free_vma(task, &info, p) < 0) {
			break;
		}

		if (info.vm_type == VMA_FREE) 
			return create_vma(task, name, p, size, flags);


		p = info.vm_base - 1;
	}

	return NULL;
}
