ENTRY(device_reset)

MEMORY
{
    DATA_RAM (rw) : ORIGIN = 0x03004000, LENGTH = 0x48000
    DSP_RAM (rx) : ORIGIN = 0x03048000, LENGTH = 0x2000
    EXTENSION_RAM (rx) : ORIGIN = 0x0304A000, LENGTH = 0x2000
    CODE_RAM (rx) : ORIGIN = 0x03050000, LENGTH = 0x2666C
    HIGH_RAM (rw) : ORIGIN = 0x0307A000, LENGTH = 0xA400
    MODULE_RAM (rw) : ORIGIN = 0x03084400, LENGTH = 0xAC00
    PROCESS_RAM (rx) : ORIGIN = 0x0308F000, LENGTH = 0x5000
    GLYPH_RAM (rw) : ORIGIN = 0x03094000, LENGTH = 0x7000
    LANGUAGE_RAM (rw) : ORIGIN = 0x0309B000, LENGTH = 0x4000
}

SECTIONS
{
    .code :
    {
        KEEP(*(.code_vectors))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        device_code_end = .;
    } > CODE_RAM

    .data :
    {
        KEEP(*(.entry_vector))
        *(.data .data.*)
        . = ALIGN(4);
    } > DATA_RAM AT > CODE_RAM

    .bss (NOLOAD) :
    {
        device_bss_start = .;
        *(.bss .bss.*)
        . = ALIGN(4);
        PROVIDE(device_profile_buckets = .);
        . += DEFINED(device_profile_bucket_shift) ? (((LENGTH(CODE_RAM) - 1) >> device_profile_bucket_shift) + 1) * 2 : 0;
        . = ALIGN(4);
        device_bss_end = .;
    } > DATA_RAM

    .noinit (NOLOAD) :
    {
        *(.noinit .noinit.*)
        . = ALIGN(4);
        device_noinit_end = .;
    } > DATA_RAM

    .high_ram (NOLOAD) :
    {
        *(.high_ram .high_ram.*)
        . = ALIGN(4);
    } > HIGH_RAM

    .glyph_ram (NOLOAD) :
    {
        *(.glyph_ram .glyph_ram.*)
        . = ALIGN(4);
    } > GLYPH_RAM

    .language_ram (NOLOAD) :
    {
        *(.language_ram .language_ram.*)
        . = ALIGN(4);
    } > LANGUAGE_RAM

    .comment 0 : { *(.comment) }
    .ARM.attributes 0 : { *(.ARM.attributes) }

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

PROVIDE(device_profile_bucket_count = ((LENGTH(CODE_RAM) - 1) >> device_profile_bucket_shift) + 1);
PROVIDE(device_profile_code_base = ORIGIN(CODE_RAM));
PROVIDE(device_profile_code_bytes = LENGTH(CODE_RAM));
PROVIDE(device_profile_overlay_base = ORIGIN(HIGH_RAM));
PROVIDE(device_profile_overlay_bytes = ORIGIN(GLYPH_RAM) - ORIGIN(HIGH_RAM));

ASSERT(ADDR(.code) == 0x03050000, "the code vector table must be at 0x03050000")
ASSERT(ADDR(.data) == 0x03004000, "the entry vector must be at 0x03004000")
ASSERT(ORIGIN(HIGH_RAM) + LENGTH(HIGH_RAM) == ORIGIN(MODULE_RAM), "the module slot must follow the decoder workspace")
ASSERT(ORIGIN(MODULE_RAM) + LENGTH(MODULE_RAM) == ORIGIN(PROCESS_RAM), "the UI slot must end where audio processing starts")
ASSERT(ORIGIN(PROCESS_RAM) + LENGTH(PROCESS_RAM) == ORIGIN(GLYPH_RAM), "audio processing must end where the glyph cache starts")
ASSERT(SIZEOF(.data) == 8, "only the entry vector may be in .data: make initialised globals const")
device_data_limit = ORIGIN(DSP_RAM);
ASSERT(ORIGIN(DSP_RAM) + LENGTH(DSP_RAM) <= 0x0304A240, "the audio module must stay below the boot ROM boundary")
ASSERT(ORIGIN(EXTENSION_RAM) == ORIGIN(DSP_RAM) + LENGTH(DSP_RAM), "the extension module must follow the audio module")
ASSERT(ORIGIN(EXTENSION_RAM) + LENGTH(EXTENSION_RAM) == 0x0304C000, "the extension module must end at the top of core 0 data RAM")
ASSERT(device_noinit_end <= device_data_limit, ".data, .bss and .noinit must end before the independent audio module")

ASSERT(ORIGIN(GLYPH_RAM) + LENGTH(GLYPH_RAM) == ORIGIN(LANGUAGE_RAM), "the language window must follow the glyph cache")
ASSERT(ORIGIN(LANGUAGE_RAM) + LENGTH(LANGUAGE_RAM) == 0x0309F000, "the language window must end below the reserved top of high RAM")
