.section .text

#include <x86-64/gdt.h>
#include <x86-64/idt.h>
#include <x86-64/memory.h>

#include <cpu.h>

.macro pushaq
    pushq %r15
	pushq %r14
	pushq %r13
	pushq %r13
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rdi
	pushq %rsi
	pushq %rbp
	pushq %rbx
	pushq %rdx
	pushq %rcx
	pushq %rax
.endm # pushaq

.macro pushrev
	pushq %rax
	pushq %rcx
	pushq %rdx
	pushq %rbx
	pushq %rbp
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13	
	pushq %r14
    pushq %r15
.endm # pushrev

.macro popaq
    popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rbp
	popq %rbx
	popq %rdx
	popq %rcx
	popq %rax
.endm # popaq


.macro ISR_NOERRCODE int_no
.global isr\int_no
isr\int_no:
	pushq $0
	pushq $\int_no
	jmp isr_common_stub
.endm

.macro ISR_ERRCODE int_no
.global isr\int_no
isr\int_no:
	pushq $\int_no
	jmp isr_common_stub
.endm

/* LAB 3: your code here. */

/* 
   Taken from idt.h and in the Intel Manual
   Interrupt numbers used for processor exceptions
 */

ISR_NOERRCODE int_no = 0
ISR_NOERRCODE int_no = 1
ISR_NOERRCODE int_no = 2
ISR_NOERRCODE int_no = 3
ISR_NOERRCODE int_no = 4
ISR_NOERRCODE int_no = 5
ISR_NOERRCODE int_no = 6
ISR_NOERRCODE int_no = 7

ISR_ERRCODE int_no = 8

ISR_ERRCODE int_no = 10
ISR_ERRCODE int_no = 11
ISR_ERRCODE int_no = 12
ISR_ERRCODE int_no = 13
ISR_ERRCODE int_no = 14

ISR_NOERRCODE int_no = 16

ISR_ERRCODE int_no = 17

ISR_NOERRCODE int_no = 18
ISR_NOERRCODE int_no = 19
ISR_NOERRCODE int_no = 30

/* Hardware timer */
ISR_NOERRCODE int_no = 32

/* Software interrupt */

ISR_NOERRCODE int_no = 128

isr_common_stub:
	/* LAB 3: your code here. */
	/* Save the register state on the stack. */
	
	/* int_frame layout:
	      ds;
	      r15, r14, r13, r12, r11, r10, r9, r8;
	      rdi, rsi, rbp, rbx, rdx, rcx, rax;
	      int_no, err_code;
	      rip, cs, rflags, rsp, ss;
	*/

	/* rip, cs, rflags, rsp, ss will be pushed by the cpu to the stack
	int_no and err_code are pushed in the ISR_ERRCODE macro */

	/* push all 64bit registers using a macro */

	pushrev

	/* push ds */

	movq %ds, %rax
	pushq %rax

	/* Recover the segments. */

	movw $GDT_KDATA, %r15w
	movw %r15w, %ds
	movw %r15w, %es

	/* Jump to the generic interrupt handler */

	movq %rsp, %rdi
	jmp int_handler

.global syscall64
syscall64:
	/* LAB 3: your code here. */
	
	/* Store the userspace stack pointer in the per-cpu struct. */
	
	/*RSP userspace stack pointer and TSS per-cpu struct*/

	/* Load the kernel stack pointer from the per-cpu struct. */

	/* Store the register state into the per-cpu struct. */

	/* Jump to the system call handler. */
	movq %r10, %rcx
	pushq %rbp
	pushq $0
	jmp syscall_handler

.global iret64
iret64:
	/* int_frame layout:
	      ds;
	      r15, r14, r13, r12, r11, r10, r9, r8;
	      rdi, rsi, rbp, rbx, rdx, rcx, rax;
	      int_no, err_code;
	      rip, cs, rflags, rsp, ss;
	*/

	/* Move stack pointer to the interrupt frame */

	mov %rdi, %rsp 

	/* Pop the %ds register before the general purpose regs
		Cannot do this directly so use intermediate register */

	xor %rax, %rax
	popq %rax
	movq %rax, %ds
	movq %rax, %es

	/* Pop all general purpose registers */
	popaq

	/* Skip the int_no and err_code vars on the stack */

	addq $16, %rsp

	/* iretq will now pop rip, cs, rflags, rsp, ss
	and set the new stack pointer to this rsp */

	iretq

.global sysret64
sysret64:
	/* LAB 3: your code here. */
	/* Recover the register state. */

	/* Return from the system call. */
	sysretq
