ENTRY(dh2_absolute_path_fix)
direct_open = 0x56ddb0;
relative_path = 0x56de34;
SECTIONS {
  . = 0x56dd9c;
  .text : { *(.text) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
