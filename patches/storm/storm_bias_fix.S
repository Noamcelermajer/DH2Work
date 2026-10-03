.syntax unified
.arm
.text
.global storm_bias_fix
.type storm_bias_fix,%function
/* hook_inline_function treated dlopen's opaque handle as an old bionic soinfo*
 * and read its private load_bias field at +0x8c. Modern bionic randomizes handles.
 * For this exact DH2 library, recover its bias from its exported JNI_OnLoad.
 * Preserve all registers except r0, just like the replaced ldr instruction.
 */
storm_bias_fix:
    push {r1, r2, r3, r4, r12, lr}
    ldr r1, name_delta
name_pc:
    add r1, pc, r1
    bl dlsym_plt
    cmp r0, #0
    beq done
    ldr r1, engine_onload
    sub r0, r0, r1
done:
    pop {r1, r2, r3, r4, r12, lr}
    b original_compare
.p2align 2
name_delta: .word jni_onload_name - (name_pc + 8)
engine_onload: .word 0x0053224c
.size storm_bias_fix,.-storm_bias_fix
.section .note.GNU-stack,"",%progbits
