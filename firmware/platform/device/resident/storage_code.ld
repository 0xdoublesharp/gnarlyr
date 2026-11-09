MEMORY
{
    STORAGE_RAM (rx) : ORIGIN = 0x0308F000, LENGTH = 0x4000
}

SECTIONS
{
    .storage_code :
    {
        LONG(0) LONG(0) LONG(0) LONG(0)
        KEEP(*(.rodata.STORAGE_CODE_ENTRY))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > STORAGE_RAM
    .data : { *(.data .data.*) } > STORAGE_RAM
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } > STORAGE_RAM
    .comment 0 : { *(.comment) }
    .ARM.attributes 0 : { *(.ARM.attributes) }
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

ASSERT(STORAGE_CODE_ENTRY == 0x0308F010, "storage entry must follow its header")
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "storage code must not own mutable globals")
