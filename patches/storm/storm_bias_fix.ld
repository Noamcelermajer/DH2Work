ENTRY(storm_bias_fix)
dlsym_plt = 0x000321d4;
original_compare = 0x000438e8;
jni_onload_name = 0x00009a06;
storm_dlopen = 0x00031f28;
storm_dlsym = 0x000321d4;
storm_fopen = 0x0003218c;
storm_puts = 0x000324b0;
storm_errno = 0x000320a8;
storm_log = 0x0003200c;
SECTIONS {
  . = 0x000d3800;
  .text : { *(.text) }
  . = ALIGN(4);
  .rodata : { *(.rodata*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
