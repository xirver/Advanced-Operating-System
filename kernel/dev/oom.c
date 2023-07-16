#include <kernel/mem/walk.h>
#include <kernel/mem/buddy.h>
#include <kernel/dev/oom.h>
#include <kernel/sched/kernel_thread.h>

#ifndef USE_BIG_KERNEL_LOCK
	extern struct spinlock console_lock;
#endif

extern pid_t pid_max;

#define DEBUG 1

/*
    This function determine which task is the one that need to be killed
    in order to save memory. The logic is to return the highest value in a range
    from -1000 to 1000, the hightest the value the higher is the probability that
    the task is going to be killed.

    Points should be added based on:
    - the pml4 of the task, if page is present;
    - the status of the task;

    We don't need to consider task that:
    - have the status unkillable;
    - have been already reaped;
    - are in the middle of a fork;

*/

extern struct list buddy_free_list[BUDDY_MAX_ORDER];

struct oom_info {
    int oom_score;
};

static int read_all_pte(physaddr_t *entry, uintptr_t base, uintptr_t end, 
    struct page_walker *walker)
{
    struct oom_info *info = walker->udata;

    if (*entry & PAGE_PRESENT) {
        info->oom_score++;
    }

    return 0;
}

int get_oom_score(struct task *task)
{
    int ret; 

    struct oom_info info = {
        .oom_score = 0,
    }; 

	struct page_walker walker = {
		.pte_callback = read_all_pte,
		.udata = &info,
	};
    
   
    ret = walk_all_pages(task->task_pml4, &walker);

    if (ret < 0)
        return -1;
        
    return info.oom_score;
}

void print_memory(uint64_t free_memory)
{

    debug_print("(CPU %d) Under memory pressure. Calling oom_kill\n", this_cpu->cpu_id);
    debug_print("(CPU %d) \tFree memory: %u\n", this_cpu->cpu_id, free_memory);
    debug_print("(CPU %d) \tMemory threshold: %u\n", this_cpu->cpu_id, MEMORY_THRESHOLD);
}

void oom_kill(uint64_t free_memory)
{
    struct task *task, *task_to_delete = NULL;
    int oom_score, highest_oom_score = 0;

#ifndef USE_BIG_KERNEL_LOCK
	spin_lock(&console_lock);
#endif

    print_memory(free_memory);

    for (pid_t pid = 1; pid < pid_max; pid++) {

        task = pid2task(pid, 0);
        if (!task) {
            continue;
        }

        // Avoid killing the current kernel thread
        if (task->task_type == TASK_TYPE_KERNEL) {
            debug_print("(CPU %d) PID %d - task is kernel type\n", this_cpu->cpu_id, pid);
            continue;
        }

        // Retrieve the score for the current task
        oom_score = get_oom_score(task);
        if (oom_score < 0) {
            debug_print("(CPU %d) PID %d - OOM Score < 0\n", this_cpu->cpu_id, pid);
            continue;
        }

        debug_print("(CPU %d) PID %d OOM score: %u\n", this_cpu->cpu_id, task->task_pid, oom_score);

        // Track the task with the largest OOM score
        if (oom_score > highest_oom_score) {
            highest_oom_score = oom_score;
            task_to_delete = task;
        }
    }

    assert(task_to_delete);

#ifndef USE_BIG_KERNEL_LOCK
	spin_unlock(&console_lock);
#endif

    // Kill and free the task with the largest OOM score
    task_destroy(task_to_delete);
}

/*
 * Get the total amount of free memory in the buddy free list 
 * by combining the size of all orders
 */
uint64_t get_total_free_memory(void)
{
    uint64_t free_memory, total_free_memory = 0;
    int cnt_per_order;
    struct list *node;

    physaddr_t pa_max = 0, pa;

	for (size_t order = 0; order < BUDDY_MAX_ORDER; ++order) {

        cnt_per_order = 0;
        list_foreach(&buddy_free_list[order], node) {
            cnt_per_order++;

            struct page_info *page;
            page = container_of(node, struct page_info, pp_node);
            pa = page2pa(page);
            if (pa > pa_max)
                pa_max = pa;
        }

        free_memory = (1 << order) * cnt_per_order * PAGE_SIZE;
        total_free_memory += free_memory;
    }

    return free_memory;
}

void oom_thread(void) 
{
    struct task *task;
    uint64_t free_memory;

    // If a task is already dying, don't do anything. 
    // Killing that task will free memory
    for (pid_t pid = 1; pid < pid_max; pid++) {
        task = pid2task(pid, 0);
        if (!task) {
            continue;
        }

        if (task->task_status == TASK_DYING)
            sched_yield();
    }

    free_memory = get_total_free_memory();
    debug_print("(CPU %d) Free memory: %d / %d\n", this_cpu->cpu_id, free_memory, MEMORY_THRESHOLD);
    if (free_memory < MEMORY_THRESHOLD) {
        // Under memory pressure: kill the task with highest OOM score
        oom_kill(free_memory);
    }

	cur_task->task_frame.rip = (uint64_t) &oom_thread;
	cur_task->task_frame.rsp = KERNEL_STACK_TOP;

	sched_yield();
}