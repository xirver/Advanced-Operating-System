#include <error.h>
#include <string.h>
#include <assert.h>
#include <cpu.h>

#include <x86-64/asm.h>
#include <x86-64/gdt.h>

#include <kernel/acpi.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/vma/syscall.h>

extern void syscall64(void);

void syscall_init(void)
{
	/* LAB 3: your code here. */
}

void syscall_init_mp(void)
{
	/* LAB 6: your code here. */
}

/*
 * Print a string to the system console.
 * The string is exactly 'len' characters long.
 * Destroys the environment on memory errors.
 */
static void sys_cputs(const char *s, size_t len)
{
	/* Check that the user has permission to read memory [s, s+len).
	 * Destroy the environment if not. */

	assert_user_mem(cur_task, (void *)s, len, 0);

	/* Print the string supplied by the user. */
	cprintf("%.*s", len, s);
}

/*
 * Read a character from the system console without blocking.
 * Returns the character, or 0 if there is no input waiting.
 */
static int sys_cgetc(void)
{
	return cons_getc();
}

/* Returns the PID of the current task. */
static pid_t sys_getpid(void)
{
	return cur_task->task_pid;
}

static int sys_kill(pid_t pid)
{
	struct task *task;

	/* LAB 5: your code here. */

	task = pid2task(pid, 1);

	if (!task) {
		return -1;
	}

	cprintf("[PID %5u] Exiting gracefully\n", task->task_pid);
	
	task_destroy(task);

	return 0;
}

static int sys_getcpuid(void)
{
	/* LAB 6: your code here. */
	return this_cpu->cpu_id;
}

/* Dispatches to the correct kernel function, passing the arguments. */
int64_t syscall(uint64_t syscallno, uint64_t a1, uint64_t a2, uint64_t a3,
        uint64_t a4, uint64_t a5, uint64_t a6)
{
	/*
	 * Call the function corresponding to the 'syscallno' parameter.
	 * Return any appropriate return value.
	 */

	switch (syscallno) {
	case SYS_cputs: 
		sys_cputs((char *)a1, (size_t)a2);
		break;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getpid:
		return sys_getpid();
	case SYS_kill:
		return sys_kill(a1);
	case SYS_mquery:
		return sys_mquery((struct vma_info *) a1, (void *) a2);
	case SYS_mmap:
		return (uint64_t) sys_mmap((void *)a1, (size_t) a2, (int) a3, (int) a4, (int) a5, (uintptr_t) a6);
	case SYS_munmap:
		sys_munmap((void *)a1, (size_t) a2);
		break;
	case SYS_mprotect:
		return sys_mprotect((void *)a1, (size_t)a2, (int)a3);
	case SYS_madvise:
		return sys_madvise((void *)a1, (size_t)a2, (int)a3);
	case SYS_yield:
		sched_yield();
		break;
	case SYS_fork:
		return sys_fork();
	case SYS_wait:
		return sys_wait((int *) a1);
	case SYS_waitpid:
		return sys_waitpid((pid_t) a1, (int *) a2, (int) a3);
	case SYS_getcpuid:
		return sys_getcpuid();
	case NSYSCALLS:
		cprintf("[syscall]: Syscall `NSYSCALLS` not implemented\n");
		return -ENOSYS;
	default:
		cprintf("[syscall]: Unknown syscall number: %d\n", syscallno);
		return -ENOSYS;
	};
	
	return 0;
}

void syscall_handler(uint64_t syscallno, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6)
{
	struct int_frame *frame;

	/* Syscall from user mode. */
	assert(cur_task);

	/* Avoid using the frame on the stack. */
	frame = &cur_task->task_frame;

	/* Issue the syscall. */
	frame->rax = syscall(syscallno, a1, a2, a3, a4, a5, a6);

	/* Return to the current task, which should be running. */
	task_run(cur_task);
}
