/* STM32F103C8T6 linker script for libopencm3 — self-contained */

/* NOTE: The datasheet specifies 64K flash for C8T6, but most parts
 * in the wild (clone Blue Pills included) actually ship with the
 * full 128K die and the extra bank is usable. Bump to 128K below
 * if your unit has it — verify with `st-info --chipid` / `st-flash`. */
MEMORY
{
    rom (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Ensure libopencm3 vector table and reset handler are pulled in */
EXTERN(vector_table)
ENTRY(reset_handler)

SECTIONS
{
    .text : {
        *(.vectors)        /* libopencm3 vector table — must be first */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } >rom

    .preinit_array : {
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
    } >rom

    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } >rom

    .fini_array : {
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        __fini_array_end = .;
    } >rom

    .data : {
        _data = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >ram AT >rom

    _data_loadaddr = LOADADDR(.data);

    .bss : {
        _bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >ram

    /* Heap and stack */
    end = .;
    _end = .;
    _stack = ORIGIN(ram) + LENGTH(ram);
}
