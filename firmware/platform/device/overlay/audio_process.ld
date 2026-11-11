MEMORY
{
    MODULE_RAM (rx) : ORIGIN = 0x0308F000, LENGTH = 0x5000
    MODULE_ZEROED (rw) : ORIGIN = 0x0308F000, LENGTH = 0x5000
}

SECTIONS
{
    .overlay :
    {
        LONG(0) LONG(0) LONG(0) LONG(0)
        module_entry = .;
        KEEP(*(.rodata.*_MODULE_ENTRY))
        module_entry_end = .;
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > MODULE_RAM

    .data : { *(.data .data.*) } > MODULE_RAM

    .bss ADDR(.overlay) + SIZEOF(.overlay) (NOLOAD) :
    {
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
    } > MODULE_ZEROED

    .comment 0 : { *(.comment) }
    .ARM.attributes 0 : { *(.ARM.attributes) }

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

ASSERT(ADDR(.overlay) == 0x0308F000, "a module must start at 0x0308F000")
ASSERT(module_entry == 0x0308F010, "the entry table must follow the 16-byte header")
ASSERT(module_entry_end - module_entry >= 8, "a module needs one entry table named <NAME>_MODULE_ENTRY")
ASSERT(SIZEOF(.data) == 0, "a module may not hold initialised data: make it const")
ASSERT(ADDR(.bss) >= ADDR(.overlay) + SIZEOF(.overlay), "a module's .bss must follow its code")

ASSERT(ADDR(.bss) + SIZEOF(.bss) <= ORIGIN(MODULE_ZEROED) + LENGTH(MODULE_ZEROED), "the processing code and private state must fit their bank")
