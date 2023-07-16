#include <lib.h>

int main(void)
{
	pid_t child, killed;
	size_t i;

	child = fork();

	if (child > 0) {
		printf("Child is %u\n", child);
		sched_yield();
		sched_yield();
		sched_yield();
		printf("[PID %5u] Killing the child!\n", getpid());
		kill(child);
		printf("[PID %5u] Waiting...\n", getpid());
		assert(waitpid(child, NULL, 0) == child);
		printf("[PID %5u] I am the parent!\n", getpid());
	} else {
		printf("[PID %5u] I am the child!\n", getpid());

		while (1);
	}

	return 0;
}

