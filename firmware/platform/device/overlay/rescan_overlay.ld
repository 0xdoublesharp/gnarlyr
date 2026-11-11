ENTRY(rescan_overlay_entry)

MEMORY
{
    HIGH_RAM (rx) : ORIGIN = 0x0307A000, LENGTH = 0x18000
}

SECTIONS
{
    .overlay :
    {
        LONG(0) LONG(0) LONG(0) LONG(0)
        KEEP(*(.rodata.RESCAN_MODULE_ENTRY))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > HIGH_RAM

    .data : { *(.data .data.*) } > HIGH_RAM
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } > HIGH_RAM

    .comment 0 : { *(.comment) }
    .ARM.attributes 0 : { *(.ARM.attributes) }

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

ASSERT(ADDR(.overlay) == 0x0307A000, "the overlay must start at 0x0307A000")
ASSERT(RESCAN_MODULE_ENTRY == 0x0307A010, "the entry table must follow the 16-byte header")
ASSERT(SIZEOF(.data) == 0, "the overlay may not hold initialised data")
ASSERT(ADDR(.bss) >= ADDR(.overlay) + SIZEOF(.overlay), "the overlay's .bss must follow its code")
