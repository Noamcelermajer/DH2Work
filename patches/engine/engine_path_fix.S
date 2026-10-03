.syntax unified
.arm
.text
.global dh2_absolute_path_fix
.type dh2_absolute_path_fix,%function
/* Enter with flags from cmp(find_colon, string_end).
 * A found colon retains the original direct-open behavior. Otherwise Android
 * absolute paths also go directly to fopen; relative names use WorkingDirectory.
 * r0 and r3 are scratch here; the next operation is the original fopen call.
 */
dh2_absolute_path_fix:
    ldr r0, [sp, #0x60]
    bne direct_open
    ldrb r3, [r0]
    cmp r3, #47
    bne relative_path
.size dh2_absolute_path_fix,.-dh2_absolute_path_fix
.section .note.GNU-stack,"",%progbits
