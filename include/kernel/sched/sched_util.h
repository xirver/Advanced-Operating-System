#pragma once

#include <task.h>
#include <assert.h>
#include <cpu.h>
#include <spinlock.h>
#include <kernel/sched/task.h>

// Enable fair scheduler vs round-robin
#define FAIR_SCHEDULER 0
#define TIMESLICE 100000000

char *get_task_type(enum task_type type);
void print_queue(struct list *q, char *name, int debug);
void print_cpu_tasks(int debug);
struct task *queue_pop_task(struct list *q);
void queue_add_task(struct list *q, struct task *task);
void queue_add_priority(struct list *q, struct task *new_task);
void move_nextq_to_runq(void);
void move_cur_task_to_nextq(void);