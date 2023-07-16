#include <types.h>
#include <cpu.h>
#include <error.h>

#include <kernel/acpi/lapic.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

#define DEBUG 1

extern struct spinlock console_lock;
extern struct list runq;
extern size_t nuser_tasks;


pid_t sys_wait(int *rstatus)
{
	/* LAB 5: your code here. */
	return sys_waitpid(-1, rstatus, 0);
}

/*
 * Free all zombies. Return the PID of the child that the parent
 * is waiting for if it has been freed.
 * 
 * Return -1: did not free child on which parent is waiting
 * Return pid: pid of child on which parent is waiting
 */
pid_t reap_zombies(struct task *task)
{
	struct list *head, *zombie_node;
	struct task *zombie;
	pid_t ret_pid = -1;

	head = &task->task_zombies;
	zombie_node = head->next;
	while (zombie_node != head) {
		zombie = container_of(zombie_node, struct task, task_node);
		cprintf("[PID %5u] Reaping task with PID %d\n", task->task_pid, zombie->task_pid);
		assert(zombie);

		if (task->task_wait == zombie)
			ret_pid = zombie->task_pid;

		zombie_node = zombie_node->next;
		list_del(&zombie->task_node);
		task_free(zombie);
	}

	return ret_pid;
}

/* If waitpid is called then we want the child to set the return value (RAX) for the parent.
To tell the child to do this we set parent_task->task_wait to non-NULL. If we're waiting
for any child, just set task_wait to the parent, because it doesn't matter what the value is.
If we're waiting for a specific child, then set wait to that child task
*/
void set_task_waiting(struct task *task, pid_t pid)
{
	struct task *child_task;

	if (pid == -1) {
		task->task_wait = task;
	} else {
		child_task = pid2task(pid, 0);
		assert(child_task);
		task->task_wait = child_task;
	}
}


pid_t sys_waitpid(pid_t pid, int *rstatus, int opts)
{
	struct task *zombie, *child_task;
	pid_t ret;

	lock_task(cur_task);

	// Check if there is a child to wait for
	if(list_is_empty(&cur_task->task_children) ||
			cur_task->task_pid == pid) {
		unlock_task(cur_task);
		return -ECHILD;
	}

	debug_print("\n\n\t(CPU %d) waitpid\n\n", this_cpu->cpu_id);

	// Mark the current task as waiting
	set_task_waiting(cur_task, pid);

	debug_print("(CPU %d) Parent waiting on task: %p\n", this_cpu->cpu_id, cur_task->task_wait);

	// Check if child on which parent is waiting has terminated
	print_zombies(cur_task);
	ret = reap_zombies(cur_task);
	if (ret > -1) {
		unlock_task(cur_task);
		return ret;
	}

	if (DEBUG) cprintf("Removing parent from runq\n");

	// Remove the waiting parent task from the run queue
	list_del(&cur_task->task_node);

	// Delete from local run queue, so don't have to lock it
	// XXX don't have to delete from local runq because it's set as current task
	//local_runq_len_set(DEC);
	nuser_tasks_set(DEC);
	unlock_task(cur_task);

	cur_task = NULL;

	sched_yield();

	panic("This point should not be reached\n");

	return -1;
}

