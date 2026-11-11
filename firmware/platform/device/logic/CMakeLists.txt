include(tremor.cmake)
include(opus.cmake)

set(ECHO_LIBMAD_DIR ${PROJECT_SOURCE_DIR}/third_party/libmad)
set(FPM_64BIT ON)
set(SIZEOF_INT 4)
configure_file(${ECHO_LIBMAD_DIR}/mad.h.in ${CMAKE_CURRENT_BINARY_DIR}/libmad/mad.h @ONLY)
add_library(echo_libmad STATIC
    ${ECHO_LIBMAD_DIR}/bit.c
    ${ECHO_LIBMAD_DIR}/fixed.c
    ${ECHO_LIBMAD_DIR}/frame.c
    ${ECHO_LIBMAD_DIR}/huffman.c
    ${ECHO_LIBMAD_DIR}/layer12.c
    ${ECHO_LIBMAD_DIR}/layer3.c
    ${ECHO_LIBMAD_DIR}/stream.c
    ${ECHO_LIBMAD_DIR}/synth.c
    ${ECHO_LIBMAD_DIR}/timer.c
    ${ECHO_LIBMAD_DIR}/version.c
)
target_include_directories(echo_libmad SYSTEM
    PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/libmad
    PRIVATE ${ECHO_LIBMAD_DIR}
)
target_compile_definitions(echo_libmad PRIVATE OPT_ACCURACY NDEBUG)
target_compile_options(echo_libmad PRIVATE -w)
if(CMAKE_CROSSCOMPILING)
    target_include_directories(echo_libmad SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/libmad_libc)
endif()

set(ECHO_FAAD_DIR ${PROJECT_SOURCE_DIR}/third_party/faad2)
add_library(echo_faad STATIC
    ${ECHO_FAAD_DIR}/bits.c
    ${ECHO_FAAD_DIR}/cfft.c
    ${ECHO_FAAD_DIR}/common.c
    ${ECHO_FAAD_DIR}/decoder.c
    ${ECHO_FAAD_DIR}/drc.c
    ${ECHO_FAAD_DIR}/error.c
    ${ECHO_FAAD_DIR}/filtbank.c
    ${ECHO_FAAD_DIR}/huffman.c
    ${ECHO_FAAD_DIR}/is.c
    ${ECHO_FAAD_DIR}/mdct.c
    ${ECHO_FAAD_DIR}/mp4.c
    ${ECHO_FAAD_DIR}/ms.c
    ${ECHO_FAAD_DIR}/output.c
    ${ECHO_FAAD_DIR}/pns.c
    ${ECHO_FAAD_DIR}/pulse.c
    ${ECHO_FAAD_DIR}/specrec.c
    ${ECHO_FAAD_DIR}/syntax.c
    ${ECHO_FAAD_DIR}/tns.c
)
target_include_directories(echo_faad SYSTEM PUBLIC ${ECHO_FAAD_DIR})
target_compile_definitions(echo_faad PRIVATE
    FIXED_POINT LC_ONLY_DECODER DISABLE_SBR FAAD_NO_ARM_ASM NDEBUG HAVE_STDINT_H HAVE_STRING_H
    PACKAGE_VERSION="2.11.4")
target_compile_options(echo_faad PRIVATE -w)

add_library(echo_device_logic STATIC
    aac_decoder.c
    audio_engine.c
    audio_math.c
    audio_resample.c
    audio_interpolate.c
    audio_dsp_entry.c
    audio_process_entry.c
    audio_ogg_entry.c
    backlight.c
    block_read.c
    board_pins.c
    battery_gauge.c
    watchdog.c
    bt_module.c
    card_write.c
    core1_control.c
    core1_heap.c
    core1_m4a.c
    core1_open.c
    core1_mp3.c
    core1_ogg.c
    core1_opus.c
    core1_stream.c
    core1_vorbis.c
    core1_vorbis_source.c
    dac_sequence.c
    debug_report.c
    flac_source.c
    dma_loop.c
    emmc_layout.c
    equalizer.c
    equalizer_design.c
    exfat.c
    exfat_create.c
    fat.c
    fat_create.c
    fat_delete.c
    fat_extents.c
    fat_layout.c
    fat_names.c
    fat_reserve.c
    flac_accel.c
    image_crc.c
    jack_sense.c
    loader_request.c
    key_ladder.c
    headset_dips.c
    headset_monitor.c
    headset_remote.c
    key_scanner.c
    m4a_stream.c
    mp3_decoder.c
    mp3_probe.c
    mp4_demux.c
    mp4_signature.c
    msc.c
    ogg_stream.c
    ogg_track.c
    opus_decode.c
    panel.c
    panic_text.c
    pc_profile.c
    pll.c
    power_history.c
    start_record.c
    sd_card.c
    stall_watch.c
    storage_mount.c
    stream_drift.c
    tick_clock.c
    update_names.c
    updater.c
    usb_audio.c
    usb_audio_sink.c
    usb_device.c
    usb_fallback.c
    usb_forced.c
    usb_plug.c
    vorbis_decoder.c
    vorbis_memory.c
    vorbis_track.c
    vop.c
    wav_source.c
)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(
        backlight.c
        battery_gauge.c
        bt_module.c
        core1_control.c
        core1_open.c
        dac_sequence.c
        debug_report.c
        emmc_layout.c
        equalizer_design.c
        exfat_create.c
        fat_create.c
        fat_delete.c
        fat_layout.c
        fat_names.c
        fat_reserve.c
        flac_source.c
        headset_dips.c
        headset_monitor.c
        headset_remote.c
        image_crc.c
        jack_sense.c
        key_ladder.c
        loader_request.c
        m4a_stream.c
        mp3_probe.c
        mp4_demux.c
    mp4_signature.c
        msc.c
        panel.c
        panic_text.c
        pll.c
        sd_card.c
        start_record.c
        storage_mount.c
        update_names.c
        updater.c
        usb_audio.c
        usb_device.c
        usb_fallback.c
        PROPERTIES COMPILE_OPTIONS -Os
    )
endif()
target_include_directories(echo_device_logic PUBLIC include)
target_link_libraries(echo_device_logic
    PUBLIC echo_core echo_hal echo_codec echo_libmad echo_faad echo_tremor echo_opus
    PRIVATE echo_options
)
