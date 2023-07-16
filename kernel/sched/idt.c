#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <x86-64/asm.h>
#include <x86-64/gdt.h>
#include <x86-64/idt.h>

#include <kernel/acpi.h>
#include <kernel/acpi/lapic.h>
#include <kernel/sched/idt.h>
#include <kernel/monitor.h>
#include <kernel/sched/syscall.h>
#include <kernel/sched/task.h>
#include <kernel/sched/sched.h>
#include <kernel/vma/pfault.h>
#include <kernel/vma/show.h>
#include <kernel/mem/dump.h>

#define DEBUG 0
#define DEBUG_INT_FRAME 0

extern struct spinlock console_lock;

extern size_t nuser_tasks;
extern size_t nkernel_tasks;

/* declare all extern isr */
extern void isr0(int int_no);
extern void isr1(int int_no);
extern void isr2(int int_no);
extern void isr3(int int_no);
extern void isr4(int int_no);
extern void isr5(int int_no);
extern void isr6(int int_no);
extern void isr7(int int_no);
extern void isr8(int int_no);
extern void isr10(int int_no);
extern void isr11(int int_no);
extern void isr12(int int_no);
extern void isr13(int int_no);
extern void isr14(int int_no);
extern void isr16(int int_no);
extern void isr17(int int_no);
extern void isr18(int int_no);
extern void isr19(int int_no);
extern void isr30(int int_no);
extern void isr32(int int_no);
extern void isr128(int int_no);

static const char *int_names[256] = {
	[INT_DIVIDE] = "Divide-by-Zero Error Exception (#DE)",
	[INT_DEBUG] = "Debug (#DB)",
	[INT_NMI] = "Non-Maskable Interrupt",
	[INT_BREAK] = "Breakpoint (#BP)",
	[INT_OVERFLOW] = "Overflow (#OF)",
	[INT_BOUND] = "Bound Range (#BR)",
	[INT_INVALID_OP] = "Invalid Opcode (#UD)",
	[INT_DEVICE] = "Device Not Available (#NM)",
	[INT_DOUBLE_FAULT] = "Double Fault (#DF)",
	[INT_TSS] = "Invalid TSS (#TS)",
	[INT_NO_SEG_PRESENT] = "Segment Not Present (#NP)",
	[INT_SS] = "Stack (#SS)",
	[INT_GPF] = "General Protection (#GP)",
	[INT_PAGE_FAULT] = "Page Fault (#PF)",
	[INT_FPU] = "x86 FPU Floating-Point (#MF)",
	[INT_ALIGNMENT] = "Alignment Check (#AC)",
	[INT_MCE] = "Machine Check (#MC)",
	[INT_SIMD] = "SIMD Floating-Point (#XF)",
	[INT_SECURITY] = "Security (#SX)",
	[INT_SYSCALL] = "Syscall",
	[IRQ_TIMER] = "IRQ Timer",
};

static struct idt_entry entries[256];
static struct idtr idtr = {
	.limit = sizeof(entries) - 1,
	.entries = entries,
};

static const char *get_int_name(unsigned int_no)
{
	if (!int_names[int_no])
		return "Unknown Interrupt";

	return int_names[int_no];
}

void print_int_frame(struct int_frame *frame)
{
#ifndef USE_BIG_KERNEL_LOCK
	spin_lock(&console_lock);
#endif


	cprintf("\nINT frame at %p\n", frame);

	/* Print the interrupt number and the name. */
	cprintf(" INT %u: %s\n",
		frame->int_no,
		get_int_name(frame->int_no));

	/* Print the error code. */
	switch (frame->int_no) {
	case INT_PAGE_FAULT:
		cprintf(" CR2 %p\n", read_cr2());
		cprintf(" ERR 0x%016llx (%s, %s, %s)\n",
			frame->err_code,
			frame->err_code & 4 ? "user" : "kernel",
			frame->err_code & 2 ? "write" : "read",
			frame->err_code & 1 ? "protection" : "not present");
		break;
	default:
		cprintf(" ERR 0x%016llx\n", frame->err_code);
	}

	/* Print the general-purpose registers. */
	cprintf(" RAX 0x%016llx"
		" RCX 0x%016llx"
		" RDX 0x%016llx"
		" RBX 0x%016llx\n"
		" RSP 0x%016llx"
		" RBP 0x%016llx"
		" RSI 0x%016llx"
		" RDI 0x%016llx\n"
		" R8  0x%016llx"
		" R9  0x%016llx"
		" R10 0x%016llx"
		" R11 0x%016llx\n"
		" R12 0x%016llx"
		" R13 0x%016llx"
		" R14 0x%016llx"
		" R15 0x%016llx\n",
		frame->rax, frame->rcx, frame->rdx, frame->rbx,
		frame->rsp, frame->rbp, frame->rsi, frame->rdi,
		frame->r8,  frame->r9,  frame->r10, frame->r11,
		frame->r12, frame->r13, frame->r14, frame->r15);

	/* Print the IP, segment selectors and the RFLAGS register. */
	cprintf(" RIP 0x%016llx"
		" RFL 0x%016llx\n"
		" CS  0x%04x"
		"            "
		" DS  0x%04x"
		"            "
		" SS  0x%04x\n",
		frame->rip, frame->rflags,
		frame->cs, frame->ds, frame->ss);

	cprintf("\n");

#ifndef USE_BIG_KERNEL_LOCK
	spin_unlock(&console_lock);
#endif

}

/* Set up the interrupt handlers. */
void idt_init(void)
{
	/* LAB 3: your code here. */
	uint64_t flags = IDT_PRESENT | IDT_PRIVL(0) | IDT_INT_GATE32;
	uint64_t flags_brk_and_sys = IDT_PRESENT | IDT_PRIVL(3) | IDT_INT_GATE32;

	set_idt_entry(&entries[INT_DIVIDE], &isr0, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_DEBUG], &isr1, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_NMI], &isr2, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_BREAK], &isr3, flags_brk_and_sys, GDT_KCODE);
	set_idt_entry(&entries[INT_OVERFLOW], &isr4, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_BOUND], &isr5, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_INVALID_OP], &isr6, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_DEVICE], &isr7, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_DOUBLE_FAULT], &isr8, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_TSS], &isr10, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_NO_SEG_PRESENT], &isr11, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_SS], &isr12, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_GPF], &isr13, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_PAGE_FAULT], &isr14, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_FPU], &isr16, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_ALIGNMENT], &isr17, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_MCE], &isr18, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_SIMD], &isr19, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_SECURITY], &isr30, flags, GDT_KCODE);
	set_idt_entry(&entries[IRQ_TIMER], &isr32, flags, GDT_KCODE);
	set_idt_entry(&entries[INT_SYSCALL], &isr128, flags_brk_and_sys, GDT_KCODE);

	load_idt(&idtr);
}

void idt_init_mp(void)
{
	/* LAB 6: your code here. */
	load_idt(&idtr);
}

void int_dispatch(struct int_frame *frame)
{
	/* Handle processor exceptions:
	 *  - Fall through to the kernel monitor on a breakpoint.
	 *  - Dispatch page faults to page_fault_handler().
	 *  - Dispatch system calls to syscall().
	 */

	debug_print("(CPU %d) - Interrupt number: %d (%s)\n", this_cpu->cpu_id, frame->int_no, get_int_name(frame->int_no));

	switch (frame->int_no) {
	case INT_PAGE_FAULT:
		page_fault_handler(frame);
		return;
	case INT_BREAK:
		monitor(frame);
		return;
	case INT_SYSCALL:
		frame->rax = syscall(frame->rdi, frame->rsi, frame->rdx, frame->rcx, frame->r8, frame->r9, frame->rbp);
		return;
	case IRQ_TIMER:
		lapic_eoi();
		scheduler();
		return;
	default: break;
	}

	/* Unexpected trap: The user process or the kernel has a bug. */
	print_int_frame(frame);

	if (frame->cs == GDT_KCODE) {
		panic("unhandled interrupt in kernel");
	} else {
		task_destroy(cur_task);
		return;
	}
}

void int_handler(struct int_frame *frame)
{
	/* The task may have set DF and some versions of GCC rely on DF being
	 * clear. */
	asm volatile("cld" ::: "cc");

	/* Check if interrupts are disabled.
	 * If this assertion fails, DO NOT be tempted to fix it by inserting a
	 * "cli" in the interrupt path.
	 */
	assert(!(read_rflags() & FLAGS_IF));

	if (DEBUG) cprintf("Incoming INT frame at %p\n", frame);

	if ((frame->cs & 3) == 3) {
		/* Interrupt from user mode. */
		assert(cur_task);

		/* Copy interrupt frame (which is currently on the stack) into
		 * 'cur_task->task_frame', so that running the task will restart at
		 * the point of interrupt. */
		cur_task->task_frame = *frame;

		/* Avoid using the frame on the stack. */
		frame = &cur_task->task_frame;
	}

#ifdef USE_BIG_KERNEL_LOCK
	extern struct spinlock kernel_lock;
	// Get the lock if we don't already have it
	if (kernel_lock.cpu != this_cpu) {
		spin_lock(&kernel_lock);
	}
#endif

	/* Dispatch based on the type of interrupt that occurred. */
	int_dispatch(frame);

	/* Return to the current task, which should be running. */
	task_run(cur_task);
}

void page_fault_handler(struct int_frame *frame)
{
	void *fault_va;
	unsigned perm = 0;
	int ret, vma_flags;

	/* Read the CR2 register to find the faulting address. */
	fault_va = (void *)read_cr2();

	/* Handle kernel-mode page faults. */
	if ((frame->cs & 3) != 3) {
		// Do this to stop all CPUs in the scheduler
		nuser_tasks = nkernel_tasks;
		cprintf("(CPU %d) [PID %5u] [%s Fault]  va %p ip %p\n",
                this_cpu->cpu_id, cur_task->task_pid,frame->err_code & 4 ? "user" : "Kernel", fault_va, frame->rip);
		print_int_frame(frame);
		show_vmas(cur_task);
		panic("\n\n\n\t(CPU %d) - Kernel page fault triggered!\n\n\n", this_cpu->cpu_id);
		//panic("(CPU %d) - Kernel page fault triggered!", this_cpu->cpu_id);
	}

	/* We have already handled kernel-mode exceptions, so if we get here, the
	 * page fault has happened in user mode.
	 */

	/* Destroy the task that caused the fault. */
	if (DEBUG_INT_FRAME) {
		cprintf("[PID %5u] user fault va %p ip %p\n",
			cur_task->task_pid, fault_va, frame->rip);
		print_int_frame(frame);
	}

	// Determine what the error codes are
	vma_flags = VM_READ;
	if (frame->err_code & 2) {
		vma_flags |= VM_WRITE;
	}

	if (frame->rip == (uint64_t) fault_va){
		vma_flags |= VM_EXEC;
	}
	
	ret = task_page_fault_handler(cur_task, fault_va, vma_flags);
	if(ret < 0)
		task_destroy(cur_task);
}
