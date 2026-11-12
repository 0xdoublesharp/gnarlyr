include(tremor.cmake)
include(opus.cmake)
include(demac.cmake)
include(rockbox_wma.cmake)

set(ECHO_LIBMAD_DIR ${PROJECT_SOURCE_DIR}/third_party/libmad)
if(CMAKE_CROSSCOMPILING)
    set(FPM_ARM ON)
else()
    set(FPM_64BIT ON)
endif()
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
    FIXED_POINT LC_ONLY_DECODER DISABLE_SBR NDEBUG HAVE_STDINT_H HAVE_STRING_H
    PACKAGE_VERSION="2.11.4")
if(NOT CMAKE_CROSSCOMPILING)
    target_compile_definitions(echo_faad PRIVATE FAAD_NO_ARM_ASM)
endif()
target_compile_options(echo_faad PRIVATE -w)

add_library(echo_device_logic STATIC
    core1_dsd.c
    dsd_track.c
    dsd_stream.c
    core1_dsd_source.c
    aiff_format.c
    core1_ape_source.c
    ape_stream.c
    core1_ape.c
    audio_ape_wma_entry.c
    core1_wma_source.c
    core1_wma.c
    asf_stream.c
    asf_track.c
    ape_track.c
    aac_decoder.c
    audio_boost.c
    audio_crossfeed.c
    audio_effects.c
    audio_engine.c
    audio_engine_stage.c
    audio_shape.c
    audio_fade.c
    audio_math.c
    audio_trig.c
    audio_resample.c
    audio_tempo.c
    audio_tempo_stage.c
    audio_interpolate.c
    audio_dsp_entry.c
    audio_process_entry.c
    audio_effects_entry.c
    audio_source_entry.c
    audio_skip.c
    audio_ogg_entry.c
    audio_dsd_entry.c
    backlight.c
    block_read.c
    board_pins.c
    battery_gauge.c
    watchdog.c
    bt_module.c
    card_format.c
    card_write.c
    core1_control.c
    core1_heap.c
    core1_m4a.c
    core1_open.c
    core1_open_mp3.c
    core1_open_flac.c
    core1_mp3.c
    core1_ogg.c
    core1_ogg_source.c
    core1_opus.c
    core1_opus_source.c
    core1_stream.c
    core1_vorbis.c
    core1_vorbis_source.c
    dac_sequence.c
    debug_report.c
    flac_source.c
    flac_source_open.c
    dma_loop.c
    emmc_layout.c
    equalizer.c
    equalizer_design.c
    exfat.c
    exfat_list.c
    exfat_create.c
    fat.c
    fat_list.c
    fat_create.c
    fat_delete.c
    fat_extents.c
    fat_layout.c
    fat_names.c
    fat_utf8.c
    fat_reserve.c
    flac_accel.c
    image_crc.c
    jack_sense.c
    loader_request.c
    key_ladder.c
    headset_dips.c
    headset_dips_view.c
    headset_monitor.c
    headset_monitor_reading.c
    headset_remote.c
    key_scanner.c
    m4a_stream.c
    mp3_decoder.c
    mp3_probe.c
    mp3_signature.c
    mp4_demux.c
    mp4_signature.c
    msc.c
    ogg_stream.c
    ogg_track.c
    opus_decode.c
    opus_track.c
    panel.c
    panic_text.c
    pc_profile.c
    pll.c
    power_history.c
    rtc_registers.c
    rtc_time.c
    start_record.c
    sd_card.c
    slot_copy.c
    stall_watch.c
    storage_mount.c
    workspace_loan.c
    stream_drift.c
    tick_clock.c
    time_file.c
    update_names.c
    update_trial.c
    updater.c
    updater_install.c
    usb_activity.c
    usb_audio.c
    usb_audio_descriptors.c
    usb_audio_hid.c
    usb_audio_sink.c
    usb_device.c
    usb_fallback.c
    usb_forced.c
    usb_leave.c
    usb_level.c
    usb_plug.c
    vorbis_decoder.c
    vorbis_memory.c
    vorbis_track.c
    vop.c
    wall_clock.c
    wav_source.c
    wide_divide.c
)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(
        backlight.c
        battery_gauge.c
        bt_module.c
        core1_control.c
        core1_open.c
        core1_open_mp3.c
        core1_open_flac.c
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
        flac_source_open.c
        headset_dips.c
        headset_monitor.c
        headset_remote.c
        image_crc.c
        jack_sense.c
        key_ladder.c
        loader_request.c
        m4a_stream.c
        mp3_probe.c
        mp3_signature.c
        mp4_demux.c
    mp4_signature.c
        msc.c
        panel.c
        panic_text.c
        pll.c
        sd_card.c
        slot_copy.c
        start_record.c
        storage_mount.c
        update_names.c
        update_trial.c
        updater.c
        updater_install.c
        usb_activity.c
        usb_audio.c
        usb_device.c
        usb_fallback.c
        usb_leave.c
        usb_level.c
        wide_divide.c
        PROPERTIES COMPILE_OPTIONS -Os
    )
endif()
target_include_directories(echo_device_logic PUBLIC include)
target_link_libraries(echo_device_logic
    PUBLIC echo_wma echo_demac echo_core echo_hal echo_codec echo_libmad echo_faad echo_tremor echo_opus
    PRIVATE echo_options
)
if(CMAKE_CROSSCOMPILING)
    target_compile_options(echo_libmad PRIVATE -O3)
    target_compile_options(echo_faad PRIVATE -O3 -flto -ffat-lto-objects)
    target_compile_options(echo_opus PRIVATE -flto -ffat-lto-objects)
endif()
