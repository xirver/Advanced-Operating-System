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
#include <kernel/sched/task.h>
#include <kernel/sched/task_util.h>

extern struct list runq;
extern struct spinlock runq_lock;
extern struct spinlock console_lock;
extern size_t nuser_tasks;
extern size_t nkernel_tasks;

#define DEBUG 1

void debug_dump(struct task *task)
{
	if (!DEBUG)
		return;

	cprintf("\n\n");
	cprintf("==============================================================\n");
	show_vmas(task);
	dump_page_tables(task->task_pml4, 0);
	cprintf("==============================================================\n");
	cprintf("\n");
}

void print_zombies(struct task *task)
{
	struct list *node, *head = &task->task_zombies;
	struct task *zombie;
	int i;

	if (!DEBUG)
		return;

#ifndef USE_BIG_KERNEL_LOCK
	spin_lock(&console_lock);
#endif

	cprintf("\n\n");
	cprintf("[PID %d] - ZOMBIES:\n", task->task_pid);
	cprintf("=============================================\n");
	for (node = head->next, i = 1; node != head; node = node->next, i++) {
		zombie = container_of(node, struct task, task_node);
		cprintf("\tZombie PID %d: %p\n", zombie->task_pid, zombie);
	}
	cprintf("=============================================\n");
	cprintf("\n\n");

#ifndef USE_BIG_KERNEL_LOCK
	spin_unlock(&console_lock);
#endif
}

void nuser_tasks_set(int set)
{
	char *s = (set == DEC ? "Decrementing" : "Incrementing");

	//debug_print("(CPU %d) %s nusers tasks: %d\n", this_cpu->cpu_id, s, nuser_tasks);

#ifdef USE_BIG_KERNEL_LOCK
	nuser_tasks += set;
#else
	spin_lock(&runq_lock);
	nuser_tasks += set;
	spin_unlock(&runq_lock);
#endif

	//debug_print("(CPU %d) %s nusers tasks: %d\n", this_cpu->cpu_id, s, nuser_tasks);
}

void local_runq_len_set(int set)
{
	char *s = (set == DEC ? "Decrementing" : "Incrementing");
	int runq_len_error = 0;

	if (set == DEC && this_cpu->runq_len < 1)
		runq_len_error = 1;

	if (runq_len_error)
		debug_print("(CPU %d) %s local runq_len: %d\n", this_cpu->cpu_id, s, this_cpu->runq_len);

	this_cpu->runq_len += set;

	if (runq_len_error) {
		debug_print("(CPU %d) %s local runq_len: %d\n", this_cpu->cpu_id, s, this_cpu->runq_len);
		print_cpu_tasks(DEBUG);
		assert(!runq_len_error);
	}
}

void lock_runq_add(struct task *task)
{
#ifdef USE_BIG_KERNEL_LOCK
	queue_add_task(&runq, task);
	nuser_tasks++;
	if (task->task_type == TASK_TYPE_KERNEL)
		nkernel_tasks++;
#else
	spin_lock(&runq_lock);
	queue_add_task(&runq, task);
	nuser_tasks++;
	if (task->task_type == TASK_TYPE_KERNEL)
		nkernel_tasks++;
	spin_unlock(&runq_lock);
#endif
}

void lock_task(struct task *task)
{
#ifndef USE_BIG_KERNEL_LOCK
	spin_lock(&task->task_lock);
	//debug_print("(CPU %d) Locked task PID %d\n", this_cpu->cpu_id, task->task_pid);
#endif
}

void unlock_task(struct task *task)
{
#ifndef USE_BIG_KERNEL_LOCK
	//debug_print("(CPU %d) Unlocking task PID %d\n", this_cpu->cpu_id, task->task_pid);
	spin_unlock(&task->task_lock);
#endif
}