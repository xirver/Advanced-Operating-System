#pragma once 

void print_zombies(struct task *task);
void debug_task(struct task *task);
void lock_runq_add(struct task *task);
void lock_task(struct task *task);
void unlock_task(struct task *task);
void nuser_tasks_set(int set);
void local_runq_len_set(int set);