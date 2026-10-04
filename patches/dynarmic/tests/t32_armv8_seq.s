.syntax unified
.thumb
@ DH2Work: encodings for the T32 ARMv8 patch test.
@ Assembled by clang --target=thumbv8a; gen_seq_inc.py turns the object into
@ t32_armv8_seq.inc (raw bytes plus name/offset table).
ldab r5, [r7]
ldah r5, [r7]
ldaex r5, [r7]
ldaexb r5, [r7]
ldaexh r5, [r7]
ldaexd r5, r6, [r7]
stlb r5, [r7]
stlh r5, [r7]
stlex r9, r5, [r7]
stlexb r9, r5, [r7]
stlexh r9, r5, [r7]
stlexd r9, r5, r6, [r7]
crc32b r5, r9, r7
crc32h r5, r9, r7
crc32w r5, r9, r7
crc32cb r5, r9, r7
crc32ch r5, r9, r7
crc32cw r5, r9, r7
@ Setup forms for the exclusive-store tests: mark the data address with the
@ matching exclusive load without touching the value register.
ldaex r10, [r7]
ldaexb r10, [r7]
ldaexh r10, [r7]
ldaexd r10, r11, [r7]
