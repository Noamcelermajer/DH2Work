@ Guest entry trampoline for the freestanding test programs.
@
@ The kernel (and this host) enters an ARM32 process at SP pointing at the Linux argument block:
@ argc, argv[], NULL, envp[], NULL, auxv pairs, AT_NULL. dh2_main() takes that pointer.
@
@ The assembler picks ARM or Thumb state from the target, so the same file serves both builds.
    .syntax unified
    .text
    .global _start
    .type _start, %function
_start:
    mov     r0, sp
    bl      dh2_main
1:  b       1b
    .size _start, . - _start

@ int dh2_svc_asm(int number, int a0, int a1, int a2, int a3, int a4, int a5)
@   r0..r3 = number, a0, a1, a2      [sp] = a3, [sp+4] = a4, [sp+8] = a5
@ Linux ARM EABI: syscall number in r7, arguments in r0..r5, trap with svc #0.
    .global dh2_svc_asm
    .type dh2_svc_asm, %function
dh2_svc_asm:
    push    {r4, r5, r7, lr}
    mov     r12, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    ldr     r3, [sp, #16]
    ldr     r4, [sp, #20]
    ldr     r5, [sp, #24]
    mov     r7, r12
    svc     #0
    pop     {r4, r5, r7, pc}
    .size dh2_svc_asm, . - dh2_svc_asm
