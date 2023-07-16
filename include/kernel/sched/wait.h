#pragma once

void print_zombies(struct task *task);
pid_t reap_zombies(struct task *task);
pid_t sys_wait(int *rstatus);
pid_t sys_waitpid(pid_t pid, int *rstatus, int opts);

