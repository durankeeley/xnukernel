/* run_native(code, st): load the register file from st, call code, store the register file back. Layout: y[512] g[128] rflags[8] */
.text
.globl run_native
run_native:
	push %rbp
	push %rbx
	push %r12
	push %r13
	push %r14
	push %r15
	push %rsi
	push %rdi
	vmovdqu 0(%rsi), %ymm0
	vmovdqu 32(%rsi), %ymm1
	vmovdqu 64(%rsi), %ymm2
	vmovdqu 96(%rsi), %ymm3
	vmovdqu 128(%rsi), %ymm4
	vmovdqu 160(%rsi), %ymm5
	vmovdqu 192(%rsi), %ymm6
	vmovdqu 224(%rsi), %ymm7
	vmovdqu 256(%rsi), %ymm8
	vmovdqu 288(%rsi), %ymm9
	vmovdqu 320(%rsi), %ymm10
	vmovdqu 352(%rsi), %ymm11
	vmovdqu 384(%rsi), %ymm12
	vmovdqu 416(%rsi), %ymm13
	vmovdqu 448(%rsi), %ymm14
	vmovdqu 480(%rsi), %ymm15
	pushq 640(%rsi)
	popfq
	mov 512(%rsi), %rax
	mov 520(%rsi), %rcx
	mov 528(%rsi), %rdx
	mov 536(%rsi), %rbx
	mov 552(%rsi), %rbp
	mov 568(%rsi), %rdi
	mov 576(%rsi), %r8
	mov 584(%rsi), %r9
	mov 592(%rsi), %r10
	mov 600(%rsi), %r11
	mov 608(%rsi), %r12
	mov 616(%rsi), %r13
	mov 624(%rsi), %r14
	mov 632(%rsi), %r15
	mov 560(%rsi), %rsi
	call *(%rsp)
	pushfq
	push %rax
	mov 24(%rsp), %rax
	mov %rcx, 520(%rax)
	mov %rdx, 528(%rax)
	mov %rbx, 536(%rax)
	mov %rbp, 552(%rax)
	mov %rsi, 560(%rax)
	mov %rdi, 568(%rax)
	mov %r8, 576(%rax)
	mov %r9, 584(%rax)
	mov %r10, 592(%rax)
	mov %r11, 600(%rax)
	mov %r12, 608(%rax)
	mov %r13, 616(%rax)
	mov %r14, 624(%rax)
	mov %r15, 632(%rax)
	pop %rcx
	mov %rcx, 512(%rax)
	pop %rcx
	mov %rcx, 640(%rax)
	vmovdqu %ymm0, 0(%rax)
	vmovdqu %ymm1, 32(%rax)
	vmovdqu %ymm2, 64(%rax)
	vmovdqu %ymm3, 96(%rax)
	vmovdqu %ymm4, 128(%rax)
	vmovdqu %ymm5, 160(%rax)
	vmovdqu %ymm6, 192(%rax)
	vmovdqu %ymm7, 224(%rax)
	vmovdqu %ymm8, 256(%rax)
	vmovdqu %ymm9, 288(%rax)
	vmovdqu %ymm10, 320(%rax)
	vmovdqu %ymm11, 352(%rax)
	vmovdqu %ymm12, 384(%rax)
	vmovdqu %ymm13, 416(%rax)
	vmovdqu %ymm14, 448(%rax)
	vmovdqu %ymm15, 480(%rax)
	vzeroupper
	add $16, %rsp
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbx
	pop %rbp
	ret
.section .note.GNU-stack,"",@progbits
