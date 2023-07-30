OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   */
  . = 0x80000000;
/*
    .代表0x80000000
 */
  .text : {
    *(.text .text.*)
/* 无论多少都对齐到4096即一个page   */
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
/* 同样无论多少都对齐到一个page   */
    . = ALIGN(0x1000);
/* 保证.的位置和_trampoline的位置为一个page，用于存放trampsec   */
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
/* 提供一个符号etext其值为.   */
    PROVIDE(etext = .);
  }
/*只读数据段*/
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }
/*可读可写数据段*/
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }
/*未初始化数据段*/
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }
/*定义一个符号end保存数据的结束地址*/
  PROVIDE(end = .);
}
