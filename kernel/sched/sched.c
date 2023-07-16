#include <types.h>
#include <cpu.h>
#include <list.h>
#include <stdio.h>

#include <x86-64/asm.h>
#include <x86-64/paging.h>

#include <kernel/mem.h>
#include <kernel/monitor.h>
#include <kernel/sched.h>
#include <kernel/sched/task_util.h>
#include <kernel/sched/sched_util.h>

#define DEBUG 0

struct list runq;

#ifndef USE_BIG_KERNEL_LOCK
	extern struct spinlock debug_lock;
	extern struct spinlock console_lock;
	extern struct spinlock runq_lock;
#endif

extern size_t nuser_tasks;
extern size_t nkernel_tasks;

// TSC variables
uint64_t start = 0, time_elapsed = 0;

void sched_init(void)
{
	// Initialize global run queue
	list_init(&runq);

	// Initialize local run/next queue for boot cpu
	sched_init_mp();

#ifndef USE_BIG_KERNEL_LOCK
	spin_init(&runq_lock, "runq_lock");
#endif

	spin_init(&debug_lock, "debug_lock");
}

void sched_init_mp(void)
{
	list_init(&this_cpu->runq);
	list_init(&this_cpu->nextq);
	this_cpu->runq_len = 0;
}

/* 
 * Our scheduler function that calls sched_yield() 
 * and uses read_tsc() to take the time at the start and end
 */
void scheduler(void)
{
	time_elapsed = read_tsc() - start;

	if (FAIR_SCHEDULER) {
		if (start != 0 && time_elapsed < TIMESLICE) {
			return;
		}
	}

	cur_task->jiffies += time_elapsed;

	start = read_tsc();
	sched_yield();
} 

void migrate_tasks_runq(size_t task_foreach_cpu)
{
	struct task *task_to_move;

	// Move tasks from global runq to local runq
	while (this_cpu->runq_len < task_foreach_cpu) {
		task_to_move = queue_pop_task(&runq);
		if (!task_to_move) {
			// Global runq is empty
			break;
		}
		queue_add_task(&this_cpu->runq, task_to_move);
		local_runq_len_set(INC);
	}

	// Move tasks from local runq to global runq 
	while (this_cpu->runq_len > task_foreach_cpu) {
		task_to_move = queue_pop_task(&this_cpu->runq);
		assert(this_cpu->runq_len >= 1);
		local_runq_len_set(DEC);
		if (!task_to_move) {
			debug_print("(CPU %d) runq_len: %d\n", this_cpu->cpu_id, this_cpu->runq_len);
			assert(task_to_move);
		}
		queue_add_task(&runq, task_to_move);
	}
}

/*
 * Try to run the next task. 
 */
int try_run_next_task()
{
	struct task *task;

	if(list_is_empty(&this_cpu->runq)) {
		return -1;
	}

	task = queue_pop_task(&this_cpu->runq);
	assert(this_cpu->runq_len >= 1);
	local_runq_len_set(DEC);
	assert(task);

	// If task_status == TASK_DYING, task_run will destroy it
	task_run(task);

	return 0;
}

#ifdef USE_BIG_KERNEL_LOCK

void BKL_migrate_tasks(size_t task_foreach_cpu)
{
	move_nextq_to_runq();

	if (this_cpu->runq_len != task_foreach_cpu)
		migrate_tasks_runq(task_foreach_cpu);
}

#else

void FGL_migrate_tasks(size_t task_foreach_cpu)
{
	move_nextq_to_runq();

	if (this_cpu->runq_len == task_foreach_cpu)
		return;

	if (spin_trylock(&runq_lock)) {
		// Global runq is free
		migrate_tasks_runq(task_foreach_cpu);
		spin_unlock(&runq_lock);
	} 
}

#endif


void release_and_acquire_lock(void)
{
#ifdef USE_BIG_KERNEL_LOCK
	spin_unlock(&kernel_lock);
	spin_lock(&kernel_lock);
#endif
}


static uint64_t cnt = 0;

void sched_yield(void)
{
	/* For Debug:
	int debug = 0;
	if (18 < cnt && cnt < 1000)
		debug = 1;
	else
		debug = 0;	
	cnt++;
	*/
	int debug = 0;

	debug_print("(CPU %d) Starting sched_yield()\n", this_cpu->cpu_id);
	print_cpu_tasks(debug);

	if (nuser_tasks == nkernel_tasks) {
		cprintf("\n\tNo tasks remaining!\n\n");
		while(1);
	}

	debug_print("(CPU %d) Moving current task to nextq\n", this_cpu->cpu_id);

	// Add the task that was running to the next queue
	move_cur_task_to_nextq();

	print_cpu_tasks(debug);

	// If the local run queue contains a task, run it
	try_run_next_task();
	
	// The number of tasks each CPU should run for load balancing
	size_t task_foreach_cpu = (nuser_tasks + (ncpus - 1)) / ncpus;

	debug_print("(CPU %d) Migrating tasks.\n", this_cpu->cpu_id);
	
#ifdef USE_BIG_KERNEL_LOCK

	// Use big kernel lock
	BKL_migrate_tasks(task_foreach_cpu);

#else

	// Use fine-grained locking
	FGL_migrate_tasks(task_foreach_cpu);

#endif
	print_cpu_tasks(debug);

	if (try_run_next_task() < 0) {
		// Release the lock to allow another task to run
		release_and_acquire_lock();
		sched_yield();
	}
}

/* For now jump into the kernel monitor. */
void sched_halt()
{

#ifdef USE_BIG_KERNEL_LOCK
	spin_unlock(&kernel_lock);
#endif

	while (1) {
		monitor(NULL);
	}
}

