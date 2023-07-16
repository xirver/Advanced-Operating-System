#include <x86-64/asm.h>

#include <cpu.h>

#include <kernel/acpi.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

#define DEBUG 1

/* While boot_cpus() is booting a given CPU, it communicates the per-core stack
 * pointer that should be loaded by boot_ap().
 */
void *mpentry_kstack;

void cpu_debug_print(struct cpuinfo *cpu)
{
	if (!DEBUG)
		return;

	cprintf("CPU ID: %d\n", cpu->cpu_id);
	cprintf("\tSTATUS: %d\n", cpu->cpu_status);
	cprintf("\tTASK: %p\n", cpu->cpu_task);
	cprintf("\tTSS->rsp[0]: %p\n", cpu->cpu_tss.rsp[0]);

}

void boot_cpus(void)
{
	extern unsigned char boot_ap16[], boot_ap_end[];
	void *code;
	struct cpuinfo *cpu;

	/* Write entry code at the reserved page at MPENTRY_PADDR. */
	code = KADDR(MPENTRY_PADDR);
	memmove(code, KADDR((physaddr_t)boot_ap16), boot_ap_end - boot_ap16);

	/* Boot each CPU one at a time. */
	for (cpu = cpus; cpu < cpus + ncpus; ++cpu) {
		/* Skip the boot CPU */
		if (cpu == boot_cpu) {
			continue;
		}

		/* Set up the kernel stack. */
		mpentry_kstack = (void *)cpu->cpu_tss.rsp[0];

		/* Start the CPU at boot_ap16(). */
		lapic_startup(cpu->cpu_id, PADDR(code));

		/* Wait until the CPU becomes ready. */
		while (cpu->cpu_status != CPU_STARTED);
	}
}

void mp_main(void)
{
	/* Eable the NX-bit. */
	write_msr(MSR_EFER, read_msr(MSR_EFER) | MSR_EFER_NXE);

	
	/* Load the kernel PML4. */
	asm volatile("movq %0, %%cr3\n" :: "r" (PADDR(kernel_pml4)));

	/* Load the per-CPU kernel stack. */
	asm volatile("movq %0, %%rsp\n" :: "r" (mpentry_kstack));

	cprintf("SMP: CPU %d starting\n", lapic_cpunum());

	/* LAB 6: your code here. */
	/* Initialize the local APIC. */
	
	lapic_init();
	cprintf("Lapic done\n");
	/* Set up segmentation, interrupts, system call support. */
	gdt_init_mp();
	cprintf("GDT INIT MP done\n");
	idt_init_mp();
	cprintf("IDT INIT MP done\n");
	syscall_init_mp(); //TO DO optional
	cprintf("SYSCALL OPT done\n");
	/* Set up the per-CPU slab allocator. */
	kmem_init_mp();
	cprintf("KMEM INIT MP done\n");

	/* Set up the per-CPU scheduler. */
	sched_init_mp(); //TO DO
	cprintf("SCHED INIT MP done\n");

	/* Notify the main CPU that we started up. */
	xchg(&this_cpu->cpu_status, CPU_STARTED);

	/* Schedule tasks. */
	/* LAB 6: remove this code when you are ready */
	/*
	asm volatile(
		"cli\n"
		"hlt\n");
	*/

#ifdef USE_BIG_KERNEL_LOCK
	extern struct spinlock kernel_lock;
	spin_lock(&kernel_lock);
#endif

	sched_yield();
}

