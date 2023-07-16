#include <types.h>
#include <list.h>
#include <paging.h>
#include <string.h>

#include <cpu.h>
#include <kernel/mem.h>
#include <kernel/mem/slab.h>
#include <kernel/acpi/lapic.h>

void debug_print_slab(size_t size)
{
	size_t index;

	if (size == 0) {
		panic("[debug_print_slab]: size == 0\n");
	}

	size = ROUNDUP(size, SLAB_ALIGN);
	index = (size / SLAB_ALIGN) - 1;
	if (index >= nslabs) {
		return;
		panic("[debug_print_slab]: index >= nslabs\n");
	}


	struct slab *slab = slabs + index;
	struct slab_info *info;
	struct slab_obj *obj;

	if (list_is_empty(&slab->partial)) {
		if (slab->obj_size == 128) {
			cprintf("\n\n\t[CPU %d]: FREELIST empty\n", lapic_cpunum());
		}
		return;
	}


	info = container_of(slab->partial.next, struct slab_info, node);

	//struct slab *slab_start = info - ;

	obj = container_of(info->free_list.next, struct slab_obj, node);

	if (slab->obj_size == 128) {
		assert(info->free_count);
		cprintf("&free_count: %p\n", &info->free_count);
		return;
		cprintf("nfree: %d\n", info->free_count);
		cprintf("\n\n\t[CPU %d]: FREELIST for slab size %d:\n", lapic_cpunum() ,slab->obj_size);
		struct list *node = info->free_list.next;
		struct slab_obj *free;
		for (int i = 0; i < info->free_count; ++i) {
			free = container_of(node, struct slab_obj, node);
			cprintf("\t\t%p\n", free);
			node = node->next; 
		}

	}


}

/* A slab allocator works by maintaining a list of slabs, which consists of one
 * or more pages. These slabs are subdivided into fixed-size chunks that
 * consist of a header of the type struct slab_obj followed by sufficient
 * memory for the actual object. Following these objects, a footer for the slab
 * of type struct slab_info can be found.
 *
 * This is a visual representation of such a slab:
 *   [ OBJ | DATA | OBJ | DATA | ... | INFO ]
 *
 * struct slab maintains a linked list of partial slabs, i.e. slabs that still
 * have some free objects available, and a linked of full slabs, i.e. slabs of
 * which all objects have been allocated.
 */

/* Allocates a new slab from the buddy allocator. Then this function locates
 * the struct slab_info footer at the end of the slab to initialize the list
 * of free objects and to set the number of free objects available. Then this
 * function iterates over the available objects to initialize them. More
 * specifically, it locates the header for each object and adds the object to
 * the free list of the slab, and to set a reference from the object header
 * back to the slab that owns the object. The slab is then added to the list
 * of partial slabs.
 */
int slab_alloc_chunk(struct slab *slab)
{
	struct page_info *page;
	struct slab_info *info;
	struct slab_obj *obj;
	char *base;
	size_t i;
	void* va;

	// Create new page for the slab
	page = page_alloc(ALLOC_ZERO);
	if (!page)
		return -1;
	assert(page->pp_ref == 0);

	va = page2kva(page);

	// Initialize slab info
	info = (struct slab_info *) ((uint8_t *) va + slab->info_off);
	//info = (struct slab_info *) va + slab->info_off;
	info->slab = slab;
	list_init(&info->node);
	list_add(&slab->partial, &info->node);
	list_init(&info->free_list);
	info->free_count = 0;

	// Initialize objects in the page and add to free list
	for (i = 0; i < slab->count; ++i) {
        obj = (struct slab_obj *) ((uint8_t *) va + (i * slab->obj_size));
        list_init(&obj->node);
        obj->info = info;
        list_add(&info->free_list, &obj->node);
        info->free_count++;
    }

	assert(info->free_count == slab->count);

	return 0;
}

/* Frees a slab by removing the slab from the partial list. Then this function
 * uses page_lookup() to look up the page to free.
 */
void slab_free_chunk(struct slab *slab, struct slab_info *info)
{
	/* LAB 3: your code here. */
	struct page_info *page;
	struct slab *va; 

	// remove slab from partial list
	list_del(&info->node);

	//va = (struct slab_info *) info - slab->info_off;
	va = (struct slab *) ((uint8_t *) info - slab->info_off);

	// page_lookup() should return the addr of the page to free
	page = page_lookup(kernel_pml4, va, NULL);
	if (!page) {
		panic("[slab_free_chunk]: page_lookup returned NULL\n");
	}

	page_free(page);
}

/* Initializes a slab allocator for the given object size as follows:
 *  - Calculate the object size by adding the object header size and aligning
 *    it to SLAB_ALIGN bytes.
 *  - Calculate the number of available objects per slab.
 *  - Fill in the actual information.
 *  - Initialize the lists of partial slabs and full slabs.
 */
void slab_setup(struct slab *slab, size_t obj_size)
{
	size_t count;

	obj_size += sizeof(struct slab_obj);
	obj_size = ROUNDUP(obj_size, 32);

	count = (PAGE_SIZE - sizeof(struct slab_info)) / obj_size;

	slab->obj_size = obj_size;
	slab->count = count;
	slab->info_off = obj_size * count;

	list_init(&slab->full);
	list_init(&slab->partial);
}

/* Allocates an object from the slab allocator.
 * This function first checks the list of partial slabs. If no partial slab is
 * available, it calls slab_alloc_chunk() to allocate a new slab.
 * It then gets the first partial slab, and then it gets the first free object
 * from that slab.
 * To allocate the object, it removes the object from the respective free list
 * and decrements the number of free objects.
 * Finally, if the free list becomes empty as a result, it removes the slab
 * from the list of partial slabs and adds it to the list of full slabs
 * instead.
 *
 * Returns the allocated object on success. Otherwise this function returns
 * NULL.
 */
void *slab_alloc(struct slab *slab)
{
	struct slab_info *info;
	struct slab_obj *obj;

	if (list_is_empty(&slab->partial) && slab_alloc_chunk(slab) < 0)
		return NULL;

	info = container_of(slab->partial.next, struct slab_info, node);

	obj = container_of(info->free_list.next, struct slab_obj, node);

	list_del(&obj->node);
	--info->free_count;

	if (list_is_empty(&info->free_list)) {
		list_del(&info->node);
		list_add(&slab->full, &info->node);
	}

	/*  Jump over the slab_obj structure and return the address of the
	 *  first byte in this slab object.
	 *
	 *  Note:
	 *
	 *      obj + 1 == (char *) obj + sizeof(struct slab_obj)
	 */

	return obj + 1;
}

/* Frees an object by adding it back to the slab allocator.
 *
 * First this function checks if the free list of the slab is empty. If it was,
 * then the slab is removed from the list of full slabs and added to the list
 * of partial slabs.
 * Then it frees the object by adding the object to the free list of the slab
 * and by increment the number of free objects.
 * If all objects in the slab are free, the slab allocator frees the entire
 * slab by calling slab_free_chunk().
 */
void slab_free(void *p)
{

	/*  Get the object metadata starting from the address of the
	 *  first byte in the actual object.
	 *
	 *  Note:
	 *
	 *    (struct slab_obj *)p - 1 == (char *) p - sizeof(struct slab_obj)
	 */

	struct slab_obj *obj = (struct slab_obj *)p - 1;
	struct slab_info *info = obj->info;
	struct slab *slab = info->slab;

	memset(p, 0, slab->obj_size - sizeof *obj);

	/* Remove the slab page from the slab->full list as we freed one chunk
	 * from this slab.
	 */
	if (list_is_empty(&info->free_list)) {
		list_del(&info->node);
		list_add(&slab->partial, &info->node);
	}

	/* Add the object back to the free list of the slab and increment
	 * the counter of free objects.
	 */
	list_add(&info->free_list, &obj->node);
	++info->free_count;

	/* Free the slab if all the objects are free. */
	if (info->free_count >= slab->count) {
		slab_free_chunk(slab, info);
	}
}
