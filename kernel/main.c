#include <cpu.h>

#include <kernel/acpi.h>
#include <kernel/console.h>
#include <kernel/dev/pci.h>
#include <kernel/mem.h>
#include <kernel/monitor.h>
#include <kernel/mp.h>
#include <kernel/pic.h>
#include <kernel/sched.h>
#include <kernel/tests.h>
#include <kernel/mem/init.h>
#include <kernel/vma.h>
#include <kernel/dev/disk.h>
#include <kernel/dev/oom.h>
#include <kernel/dev/swap.h>
#include <kernel/sched/kernel_thread.h>

#include <boot.h>
#include <stdio.h>
#include <string.h>

#define DEBUG 0

void kmain(struct boot_info *boot_info)
{
	extern char edata[], end[];
	struct rsdp *rsdp;

	/* Before doing anything else, complete the ELF loading process.
	 * Clear the uninitialized global data (BSS) section of our program.
	 * This ensures that all static/global variables start out zero.
	 */
	memset(edata, 0, end - edata);

	/* Initialize the console.
	 * Can't call cprintf until after we do this! */
	cons_init();
	cprintf("\n");

	/* Set up segmentation, interrupts and system calls. */
	gdt_init();
	idt_init();
	syscall_init();

	/* Initialize swap list */
	initialize_swap_list();
		

	/* Lab 1 memory management initialization functions */
	mem_init(boot_info);

	/* Set up the slab allocator. */
	kmem_init();

	/* Set up the interrupt controller and timers */
	pic_init();
	rsdp = rsdp_find();
	madt_init(rsdp);
	lapic_init();
	hpet_init(rsdp);
	pci_init(rsdp);


	/* Set up the tasks. */
	task_init();
	sched_init();

#ifdef USE_BIG_KERNEL_LOCK
	cprintf("\n\n\tUsing Big Kernel Lock\n\n");
	extern struct spinlock kernel_lock;
	spin_lock(&kernel_lock);
#else
	cprintf("\n\n\tUsing Fine-Grained Locking\n\n");
#endif


#if defined(TEST)
	TASK_CREATE(TEST, TASK_TYPE_USER);

	//run_disks();


	/* Boot CPUs */
	mem_init_mp();
	boot_cpus();

	//create_kernel_thread((uint64_t) &oom_thread);

	create_kernel_thread((uint64_t) &swap_thread);

	sched_yield();
#else
	lab3_check_kmem();

	/* Drop into the kernel monitor. */
	while (1)
		monitor(NULL);
#endif
}

/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void _panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	/* Be extra sure that the machine is in as reasonable state */
	__asm __volatile("cli; cld");

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* Break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* Like panic, but don't. */
void _warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}

