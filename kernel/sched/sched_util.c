#include <kernel/sched/sched_util.h>
#include <kernel/sched/task_util.h>

#define DEBUG 1

extern struct list runq;

#ifndef USE_BIG_KERNEL_LOCK
	extern struct spinlock runq_lock;
	extern struct spinlock console_lock;
	extern struct spinlock debug_lock;
#endif

char *get_task_type(enum task_type type) 
{
	switch(type)
	{
		case TASK_TYPE_USER:
			return "USER";
		case TASK_TYPE_KERNEL:
			return "KERNEL";
		default:
			return "ERROR";
			panic("Unknown task type\n");
	}

	return NULL;
}

/* 
 * Function used for debugging 
 */
void print_queue(struct list *q, char *name, int debug)
{
	struct list *node;
	struct task *task;

	if (!debug)
		return;

	cprintf("\n");
	cprintf("\t[CPU %d] - %s:\n", this_cpu->cpu_id, name);
	cprintf("\t=============================================\n");
	list_foreach(q, node) {
		task = container_of(node, struct task, task_node);
		cprintf("\t\t      PID %d: %s\n", task->task_pid, get_task_type(task->task_type));
	}
	cprintf("\t=============================================\n");
	cprintf("\n");
}

void print_cpu_tasks(int debug)
{
	if (!debug)
		return;

#ifndef USE_BIG_KERNEL_LOCK
	spin_lock(&console_lock);
#endif

	spin_lock(&debug_lock);

	// Print current task
	cprintf("\n\t[CPU %d] - Local run queue length: %d\n", this_cpu->cpu_id, this_cpu->runq_len);
	cprintf("\n\t[CPU %d] - Current task:\n", this_cpu->cpu_id);
	cprintf("\t=============================================\n");
	if (cur_task)
		cprintf("\t\t      PID %d: %s\n", cur_task->task_pid, get_task_type(cur_task->task_type));
	cprintf("\t=============================================\n");
	cprintf("\n");

	// Print queues
	print_queue(&runq, "Global Run Queue", debug);
	print_queue(&this_cpu->runq, "Local Run Queue", debug);
	print_queue(&this_cpu->nextq, "Local Next Queue", debug);

	spin_unlock(&debug_lock);

#ifndef USE_BIG_KERNEL_LOCK
	spin_unlock(&console_lock);
#endif

}

/*
 * Get task from the front of the queue
 *
 * NOTE:
 * 		Does not decrement queue counter
 */
struct task *queue_pop_task(struct list *q)
{
	struct list *task_node;

	task_node = list_pop_tail(q);

	if (!task_node)
		return NULL;

	return container_of(task_node, struct task, task_node);
}

void queue_add_priority(struct list *q, struct task *new_task)
{
	struct list *head, *task_node;
	struct task *task;

	list_foreach(q, task_node) {
		task = container_of(task_node, struct task, task_node);
		if (new_task->jiffies < task->jiffies) {
			break;
		}
	}
	list_insert_before(task_node, &new_task->task_node);
}

/*
 * Add task to queue
 *
 * NOTE:
 * 		Does not increment queue counter
 */
void queue_add_task(struct list *q, struct task *task)
{
	struct list *task_node = &task->task_node;

	if (FAIR_SCHEDULER) {
		queue_add_priority(q, task);
	} else {
		list_add_tail(q, task_node);
	}
}

void move_nextq_to_runq(void)
{
	struct list *head_task_node = list_head(&this_cpu->nextq);
	if (!head_task_node)
		return;
	list_del(&this_cpu->nextq);
	list_insert_before(head_task_node, &this_cpu->runq);
}

void move_cur_task_to_nextq(void)
{
	if (!cur_task)
		return;

	if (!(cur_task->task_pid > 0 && cur_task->task_pid <= PIDMAP_LIM)) {
		print_cpu_tasks(DEBUG);
		assert(cur_task->task_pid > 0 && cur_task->task_pid <= PIDMAP_LIM);
	}
	queue_add_task(&this_cpu->nextq, cur_task);
	local_runq_len_set(INC);
}