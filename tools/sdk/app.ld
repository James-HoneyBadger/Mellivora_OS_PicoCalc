ENTRY(mapp_main)

SECTIONS
{
    . = 0;
    .app_image : ALIGN(4)
    {
        __app_image_start = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        *(.data .data.*)
        . = ALIGN(4);
        __app_image_end = .;
    }

    .app_bss (NOLOAD) : ALIGN(4)
    {
        __app_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __app_bss_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
