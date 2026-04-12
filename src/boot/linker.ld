ENTRY(_start)

SECTIONS
{
    . = 0x00E00000;

    .text : {
        *(.multiboot)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    _edata = .;

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}
