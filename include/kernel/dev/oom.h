#include <types.h>
#include <stdio.h>
#include <task.h>
#include <kernel/sched/task.h>

#define MEMORY_THRESHOLD (30000 * PAGE_SIZE)

uint64_t get_total_free_memory(void);
void oom_thread(void);