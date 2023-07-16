#include <error.h>
#include <string.h>
#include <paging.h>
#include <task.h>
#include <cpu.h>
#include <rbtree.h>
#include <atomic.h>

#include <kernel/acpi.h>
#include <include/elf.h>
#include <kernel/monitor.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/vma/insert.h>
#include <kernel/vma/populate.h>
#include <kernel/vma/show.h>
#include <kernel/vma/remove.h>
#include <kernel/mem/dump.h>


#define DEBUG 1

extern struct list buddy_free_list[BUDDY_MAX_ORDER];
extern struct list runq;
extern struct list zero_list;
extern struct spinlock runq_lock;
extern struct spinlock console_lock;

pid_t pid_max = 1 << 16;
struct task **tasks = (struct task **)PIDMAP_BASE;
size_t nuser_tasks = 0;
size_t nkernel_tasks = 0;

/* Looks up the respective task for a given PID.
 * If check_perm is non-zero, this function checks if the PID maps to the
 * current task or if the current task is the parent of the task that the PID
 * maps to.
 */
struct task *pid2task(pid_t pid, int check_perm)
{
	struct task *task;

	/* PID 0 is the current task. */
	if (pid == 0) {
		return cur_task;
	}

	/* Limit the PID. */
	if (pid >= pid_max) {
		return NULL;
	}

	/* Look up the task in the PID map. */
	task = tasks[pid];

	/* No such mapping found. */
	if (!task) {
		return NULL;
	}

	/* If we don't have to do a permission check, we can simply return the
	 * task.
	 */
	if (!check_perm) {
		return task;
	}

	/* Check if the task is the current task or if the current task is the
	 * parent. If not, then the current task has insufficient permissions.
	 */
	if (task != cur_task && task->task_ppid != cur_task->task_pid) {
		return NULL;
	}

	return task;
}

void task_init(void)
{
	/* Allocate an array of pointers at PIDMAP_BASE to be able to map PIDs
	 * to tasks.
	 */

	extern struct page_table *kernel_pml4;
	size_t tasks_array_size = pid_max * sizeof(struct task *);
	uint64_t flags = (PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXEC);

	populate_region(kernel_pml4, tasks, tasks_array_size, flags);

	for (size_t pid = 0; pid < pid_max; ++pid) {
		tasks[pid] = NULL;
	}
}

/* Sets up the virtual address space for the task. */
static int task_setup_vas(struct task *task)
{
	struct page_info *page;
	extern struct page_table *kernel_pml4;
	struct page_table *pml4;

	/* Allocate a page for the page table. */

	page = page_alloc(ALLOC_ZERO);

	if (!page) {
		return -ENOMEM;
	}

	++page->pp_ref;

	pml4 = (struct page_table *) page2kva(page);

	task->task_pml4 = pml4;

	// Copy all page table entries from kernel space
	for (size_t i = PML4_INDEX(KERNEL_VMA); i < PAGE_TABLE_ENTRIES; ++i) {
		memcpy((void *) &task->task_pml4->entries[i], (void *) &kernel_pml4->entries[i], sizeof(physaddr_t));
	}

	return 0;
}

/* Allocates and initializes a new task.
 * On success, the new task is returned.
 */

struct task *task_alloc(pid_t ppid)
{
	struct task *task;
	pid_t pid;

	/* Allocate a new task struct. */
	task = kmalloc(sizeof *task);

	if (!task) {
		return NULL;
	}

	/* Set up the virtual address space for the task. */
	if (task_setup_vas(task) < 0) {
		kfree(task);
		return NULL;
	}

	/* Find a free PID for the task in the PID mapping and associate the
	 * task with that PID.
	 */
	for (pid = 1; pid < pid_max; ++pid) {
		if (!tasks[pid]) {
			tasks[pid] = task;
			task->task_pid = pid;
			break;
		}
	}

	/* We are out of PIDs. */
	if (pid == pid_max) {
		kfree(task);
		//unlock here
		return NULL;
	}

	//and unlock here

	/* Set up the task. */
	task->task_ppid = ppid;
	//task->task_type = TASK_TYPE_USER;
	task->task_status = TASK_RUNNABLE;
	task->task_runs = 0;

	memset(&task->task_frame, 0, sizeof task->task_frame);

	task->task_frame.ds = GDT_UDATA | 3;
	task->task_frame.ss = GDT_UDATA | 3;
	task->task_frame.rsp = USTACK_TOP;
	task->task_frame.cs = GDT_UCODE | 3;
	task->task_frame.rflags = FLAGS_IF;

	/* You will set task->task_frame.rip later. */

	/* LAB 4 TODO: initialize task->task_rb and task->task_mmap */
	list_init(&task->task_mmap);
	rb_init(&task->task_rb);
	list_init(&task->task_node);
	list_init(&task->task_child);
	list_init(&task->task_children);
	list_init(&task->task_zombies);


	cprintf("[PID %5u] New task with PID %u\n",
	        cur_task ? cur_task->task_pid : 0, task->task_pid);

	return task;
}

static const char *elf_get_name(struct elf_proghdr *ph)
{
	if(ph->p_flags & ELF_PROG_FLAG_EXEC)
		return ".text";
	if(ph->p_flags & ELF_PROG_FLAG_WRITE)
		return ".data";

	return ".rodata";
}

static void load_elf_segments(struct elf *elf, struct task *task, uint8_t *binary)
{
	const char *ph_name;
	void *ph_va, *ph_src;
	size_t ph_size, ph_len;
	uint64_t page_flags, vma_flags;
	struct elf_proghdr *ph, *eph;

	// Create VMA for each program header
	ph = (struct elf_proghdr *) ((uint8_t *) elf + elf->e_phoff);
	eph = ph + elf->e_phnum;
	for (; ph < eph; ph++){
		if(ph->p_type != ELF_PROG_LOAD)
			continue;

		ph_va = (void *) ph->p_va;

		// Check if program headers lie in kernel space
		if ((uint64_t) ph_va > USER_LIM) {
			panic("Malicious input detected: program headers mapping to kernel space");
		}

		ph_name = elf_get_name(ph);
		ph_size = ph->p_memsz;
		ph_src = (uint8_t *) elf + ph->p_offset;
		ph_len = ph->p_filesz;

		// Get flags in VMA format and add user privileges
		page_flags = convert_flags_from_elf_to_pages(ph) | PAGE_USER;
		vma_flags = convert_flags_from_pages_to_vma(page_flags);

		add_executable_vma(task, (char *) ph_name, ph_va, ph_size, vma_flags, ph_src, ph_len);
	}
}

/* Sets up the initial program binary, stack and processor flags for a user
 * process.
 * This function is ONLY called during kernel initialization, before running
 * the first user-mode environment.
 *
 * This function loads all loadable segments from the ELF binary image into the
 * task's user memory, starting at the appropriate virtual addresses indicated
 * in the ELF program header.
 * At the same time it clears to zero any portions of these segments that are
 * marked in the program header as being mapped but not actually present in the
 * ELF file, i.e., the program's .bss section.
 *
 * All this is very similar to what our boot loader does, except the boot
 * loader also needs to read the code from disk. Take a look at boot/main.c to
 * get some ideas.
 *
 * Finally, this function maps one page for the program's initial stack.
 */
static void task_load_elf(struct task *task, uint8_t *binary)
{
	/* Hints:
	 * - Load each program segment into virtual memory at the address
	 *   specified in the ELF section header.
	 * - You should only load segments with type ELF_PROG_LOAD.
	 * - Each segment's virtual address can be found in p_va and its
	 *   size in memory can be found in p_memsz.
	 * - The p_filesz bytes from the ELF binary, starting at binary +
	 *   p_offset, should be copied to virtual address p_va.
	 * - Any remaining memory bytes should be zero.
	 * - Use populate_region() and protect_region().
	 * - Check for malicious input.
	 *
	 * Loading the segments is much simpler if you can move data directly
	 * into the virtual addresses stored in the ELF binary.
	 * So in which address space should we be operating during this
	 * function?
	 *
	 * You must also do something with the entry point of the program, to
	 * make sure that the task starts executing there.
	 */

	struct elf *elf = (struct elf *) binary;
	uint64_t page_flags;
	int vma_flags;

	if (elf->e_magic != ELF_MAGIC) {
		panic("bad ELF");
	} 

	if (elf->e_entry > USER_LIM) {
		panic("Malicious input detected: entry point lies in kernel space");
	}

	task->task_frame.rip = elf->e_entry;

	// Temporarily switch to this tasks pml4 so that we can initialize the memory for the ELF segments
	physaddr_t old_cr3 = read_cr3();
	load_pml4((struct page_table *) PADDR(task->task_pml4));

	// Load ELF segments
	load_elf_segments(elf, task, binary);

	// Map one page for the program's initial stack 
	page_flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_NO_EXEC; 
	vma_flags = convert_flags_from_pages_to_vma(page_flags);
	add_anonymous_vma(task, "stack", (void *) USTACK_TOP - PAGE_SIZE, PAGE_SIZE, vma_flags);

	// Return to the old pml4
	load_pml4((struct page_table *) old_cr3);
}

/* Allocates a new task with task_alloc(), loads the named ELF binary using
 * task_load_elf() and sets its task type.
 * If the task is a user task, increment the number of user tasks.
 * This function is ONLY called during kernel initialization, before running
 * the first user-mode task.
 * The new task's parent PID is set to 0.
 */
void task_create(uint8_t *binary, enum task_type type)
{
	struct task *task = task_alloc(0);
	if (!task) {
		panic("Error: task_alloc failed\n");
	}

	task->task_type = type;
	task_load_elf(task, binary);

	if (task->task_type == TASK_TYPE_USER) {
		queue_add_task(&runq, task);
		nuser_tasks++;
	}
}

/* Free the task and all of the memory that is used by it.
 */
void task_free(struct task *task)
{
	struct task *waiting;

	/* If we are freeing the current task, switch to the kernel_pml4
	 * before freeing the page tables, just in case the page gets re-used.
	 */
	if (task == cur_task) {
		load_pml4((struct page_table *)PADDR(kernel_pml4));
	}

	/* Unmap the task from the PID map. */
	tasks[task->task_pid] = NULL;

	/*
	spin_lock(&console_lock);
	dump_page_tables(task->task_pml4, 0);
	spin_unlock(&console_lock);
	*/

	/* Unmap the user pages. */
	unmap_user_pages(task->task_pml4);

	/* Note the task's demise. */
	cprintf("[PID %5u] Freed task with PID %u\n", cur_task ? cur_task->task_pid : 0,
	    task->task_pid);

	/* Free the task. */
	free_vmas(task);	
	kfree(task);
}

/*
 * If the task has a parent and if the parent is not dying, add the child to the zombie
 * list. Otherwise free the task.
 * 
 * If task gets freed, don't unlock it - the task no longer exists
 */
void make_zombie_or_free(struct task *task, struct task *parent_task)
{
	lock_task(parent_task);

	if (parent_task->task_status == TASK_DYING) {
		debug_print("(CPU %d) Parent status is dying. Freeing task.\n", this_cpu->cpu_id);
		task_free(task);
	} else {
		list_del(&task->task_child);

		// Add child to parent zombies list
		assert(parent_task->task_pid != 0);
		debug_print("(CPU %d) Adding zombie with PID %d to parent with PID %d\n", 
				this_cpu->cpu_id, task->task_pid, parent_task->task_pid);
		list_add(&parent_task->task_zombies, &task->task_node);

		// If parent is waiting, add it back to runq and set return value
		if (parent_task->task_wait) {
			debug_print("(CPU %d) Add parent back to runq\n", this_cpu->cpu_id);
			parent_task->task_frame.rax = task->task_pid;
			lock_runq_add(parent_task);
		}
		unlock_task(parent_task);
	}
}


/* Frees the task. If the task is the currently running task, then this
 * function runs a new task (and does not return to the caller).
 */
void task_destroy(struct task *task)
{
	struct spinlock *lock;
	struct task *parent_task, *zombie;
	struct list *head, *task_node;

	debug_print("(CPU %d) Destroying task PID = %d\n", this_cpu->cpu_id, task->task_pid);

	if (!(task->task_pid > 0 && task->task_pid <= PIDMAP_LIM)) {
		print_cpu_tasks(DEBUG);
		debug_print("(CPU %d) Error: Invalid PID: %d\n", this_cpu->cpu_id, task->task_pid);
		assert(task->task_pid > 0 && task->task_pid <= PIDMAP_LIM);
	}

	lock_task(task);
	task->task_status = TASK_DYING;
	reap_zombies(task);
	unlock_task(task);

	if (task != cur_task) {
		// Task is getting killed by its parent - it's not running so remove it from runq
		debug_print("(CPU %d) Task PID %d is getting killed by its parent PID %d\n", 
					this_cpu->cpu_id, task->task_pid, task->task_ppid);
		sched_yield();
	}

	// Check if task has a parent and if parent is still alive
	parent_task = NULL;
	if (task->task_ppid > 0) {
		parent_task = pid2task(task->task_ppid, 0);
	}

	// Make task zombie or free it
	if (parent_task) {
		make_zombie_or_free(task, parent_task);
	} else {
		debug_print("(CPU %d) No parent exists for this task (PID %d). Freeing task.\n", this_cpu->cpu_id, task->task_pid);
		task_free(task);
	}

	nuser_tasks_set(DEC);

	cur_task = NULL;

	if (nuser_tasks > nkernel_tasks) {
		debug_print("(CPU %d) More tasks remaining: nuser_tasks: %d\n", this_cpu->cpu_id, nuser_tasks);
		sched_yield();
	}

	atomic_barrier();
	cprintf("Destroyed the only task - nothing more to do!\n");

	while (1) {
		monitor(NULL);
	}
}

/*
 * Restores the register values in the trap frame with the iretq or sysretq
 * instruction. This exits the kernel and starts executing the code of some
 * task.
 *
 * This function does not return.
 */
void task_pop_frame(struct int_frame *frame)
{
	switch (frame->int_no) {
#ifdef LAB3_SYSCALL
	case 0x80: sysret64(frame); break;
#endif
	default: iret64(frame); break;
	}

	panic("We should have gone back to userspace!");
}

/* Context switch from the current task to the provided task.
 * Note: if this is the first call to task_run(), cur_task is NULL.
 *
 * This function does not return.
 */
void task_run(struct task *task)
{
	/*
	 * Step 1: If this is a context switch (a new task is running):
	 *     1. Set the current task (if any) back to
	 *        TASK_RUNNABLE if it is TASK_RUNNING (think about
	 *        what other states it can be in),
	 *     2. Set 'cur_task' to the new task,
	 *     3. Set its status to TASK_RUNNING,
	 *     4. Update its 'task_runs' counter,
	 *     5. Use load_pml4() to switch to its address space.
	 * Step 2: Use task_pop_frame() to restore the task's
	 *     registers and drop into user mode in the
	 *     task.
	 *
	 * Hint: This function loads the new task's state from
	 *  e->task_frame.  Go back through the code you wrote above
	 *  and make sure you have set the relevant parts of
	 *  e->task_frame to sensible values.
	 */

	if (cur_task) {
		if (cur_task->task_status == TASK_RUNNING) {
			cur_task->task_status = TASK_RUNNABLE;
		} else if(cur_task->task_status == TASK_DYING ||
					cur_task->task_status == TASK_NOT_RUNNABLE) {
			task_destroy(cur_task);
		}
	}

	cur_task = task;
	if (cur_task->task_status == TASK_DYING)
		task_destroy(cur_task);

	cur_task->task_status = TASK_RUNNING;
	cur_task->task_runs++;

	load_pml4((struct page_table *) PADDR(task->task_pml4));

	debug_print("(CPU %d) Running task PID %d!\n", this_cpu->cpu_id, task->task_pid);

#ifdef USE_BIG_KERNEL_LOCK
	extern struct spinlock kernel_lock;
	assert(kernel_lock.locked);
	assert(kernel_lock.cpu == this_cpu);
	spin_unlock(&kernel_lock);
#endif

	assert(task->task_pid > 0 && task->task_pid <= PIDMAP_LIM);
	task_pop_frame(&task->task_frame);
}
