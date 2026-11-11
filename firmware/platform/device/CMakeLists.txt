add_executable(echo_device
    board/audio.c
    board/audio_dsp.c
    board/audio_process.c
    board/bluetooth.c
    board/clock.c
    board/clock_switch.c
    board/core1.c
    board/cru.c
    board/dac.c
    board/dmac.c
    board/display.c
    board/emmc.c
    board/extension.c
    board/glyphs.c
    board/gpio.c
    board/jack.c
    board/language.c
    board/loader_reboot.c
    board/high_ram.c
    board/input.c
    board/libc.c
    board/loop_bench.c
    board/internal_volume.c
    board/main.c
    board/mmc.c
    board/overlay.c
    board/panic.c
    board/power.c
    board/sd.c
    board/start_info.c
    board/watchdog.c
    board/startup.c
    board/storage.c
    board/system.c
    board/update_request.c
    board/usb.c
    board/usb_listen.c
    board/usb_report.c
)
target_sources(echo_device PRIVATE board/panel_vop.c)
if(NOT ECHO_RELEASE)
    target_sources(echo_device PRIVATE board/debug_session.c board/profiler.c)
    set(ECHO_PROFILE_BUCKET_SHIFT 7)
endif()
set(ECHO_CORE1_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/core1/core1.ld)
add_executable(echo_core1
    core1/main.c
    core1/startup.c
    board/libc.c
    board/loop_bench.c
)
set_target_properties(echo_core1 PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_CORE1_LINKER_SCRIPT}
)
target_include_directories(echo_core1 PRIVATE board)
target_link_libraries(echo_core1 PRIVATE echo_codec echo_device_logic echo_options)
target_link_options(echo_core1 PRIVATE
    -fno-lto
    -T${ECHO_CORE1_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_core1.map
)
add_executable(echo_core1_mp3
    core1/mp3_main.c
    core1/startup.c
    core1/no_heap.c
    board/libc.c
)
set(ECHO_CORE1_MP3_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/core1/core1_mp3.ld)
set_target_properties(echo_core1_mp3 PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_CORE1_MP3_LINKER_SCRIPT}
)
target_include_directories(echo_core1_mp3 PRIVATE board)
target_link_libraries(echo_core1_mp3 PRIVATE echo_device_logic echo_libmad echo_options gcc)
target_link_options(echo_core1_mp3 PRIVATE
    -fno-lto
    -T${ECHO_CORE1_MP3_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_core1_mp3.map
)
set(ECHO_CORE1_MP3_BINARY ${CMAKE_CURRENT_BINARY_DIR}/core1_mp3.bin)
add_custom_command(
    OUTPUT ${ECHO_CORE1_MP3_BINARY}
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:echo_core1_mp3> core1_mp3.bin
    DEPENDS echo_core1_mp3
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
add_executable(echo_core1_m4a
    core1/m4a_main.c
    core1/startup.c
    core1/heap.c
    board/libc.c
)
set(ECHO_CORE1_M4A_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/core1/core1_m4a.ld)
set_target_properties(echo_core1_m4a PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_CORE1_M4A_LINKER_SCRIPT}
)
target_include_directories(echo_core1_m4a PRIVATE board)
target_link_libraries(echo_core1_m4a PRIVATE echo_device_logic echo_faad echo_codec echo_options gcc)
target_link_options(echo_core1_m4a PRIVATE
    -fno-lto
    -T${ECHO_CORE1_M4A_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_core1_m4a.map
)
set(ECHO_CORE1_M4A_BINARY ${CMAKE_CURRENT_BINARY_DIR}/core1_m4a.bin)
add_custom_command(
    OUTPUT ${ECHO_CORE1_M4A_BINARY}
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:echo_core1_m4a> core1_m4a.bin
    DEPENDS echo_core1_m4a
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
add_executable(echo_core1_vorbis
    core1/vorbis_main.c
    core1/startup.c
    board/libc.c
)
set(ECHO_CORE1_VORBIS_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/core1/core1_vorbis.ld)
set_target_properties(echo_core1_vorbis PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_CORE1_VORBIS_LINKER_SCRIPT}
)
target_include_directories(echo_core1_vorbis PRIVATE board)
target_link_libraries(echo_core1_vorbis PRIVATE echo_device_logic echo_tremor echo_options gcc)
target_link_options(echo_core1_vorbis PRIVATE
    -fno-lto
    -T${ECHO_CORE1_VORBIS_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_core1_vorbis.map
)
set(ECHO_CORE1_VORBIS_BINARY ${CMAKE_CURRENT_BINARY_DIR}/core1_vorbis.bin)
add_custom_command(
    OUTPUT ${ECHO_CORE1_VORBIS_BINARY}
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:echo_core1_vorbis> core1_vorbis.bin
    DEPENDS echo_core1_vorbis
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
add_executable(echo_core1_opus
    core1/opus_main.c
    core1/startup.c
    board/libc.c
)
set(ECHO_CORE1_OPUS_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/core1/core1_opus.ld)
set_target_properties(echo_core1_opus PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_CORE1_OPUS_LINKER_SCRIPT}
)
target_include_directories(echo_core1_opus PRIVATE board)
target_link_libraries(echo_core1_opus PRIVATE echo_device_logic echo_opus echo_options gcc)
target_link_options(echo_core1_opus PRIVATE
    -fno-lto
    -T${ECHO_CORE1_OPUS_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_core1_opus.map
)
set(ECHO_CORE1_OPUS_BINARY ${CMAKE_CURRENT_BINARY_DIR}/core1_opus.bin)
add_custom_command(
    OUTPUT ${ECHO_CORE1_OPUS_BINARY}
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:echo_core1_opus> core1_opus.bin
    DEPENDS echo_core1_opus
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
set(ECHO_CORE1_BINARY ${CMAKE_CURRENT_BINARY_DIR}/core1.bin)
add_custom_command(
    OUTPUT ${ECHO_CORE1_BINARY}
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:echo_core1> core1.bin
    DEPENDS echo_core1
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

set(ECHO_OVERLAY_IMPORTS
    app_remember_dac_volume
    app_show_notice
    app_show_usb_status
    app_usb_battery_changed
    board_audio_set_attenuation
    board_audio_stream_close
    board_audio_stream_consumed
    board_audio_stream_open
    board_audio_stream_pause
    board_audio_stream_play
    board_audio_stream_ring
    board_audio_volume_level
    board_display_service
    board_wait_for_key
    board_wait_for_power_hold
    emmc_read
    emmc_start
    emmc_volume_read
    emmc_volume_write
    emmc_write
    hal_audio_service
    hal_audio_set_volume
    hal_clock_milliseconds
    hal_input_next_transition
    hal_power_battery
    hal_power_off
    i18n_format
    i18n_format_counts
    i18n_format_text
    i18n_text
    image_crc_update
    internal_volume_remount
    sd_read
    sd_sector_count
    sd_start
    sd_write
    usb_connect
    usb_flush_fifos
    usb_power_down
    usb_report_append_hex
    usb_session_stop_listening
    usb_silence_audio
    usb_vbus_present
)
if(NOT ECHO_RELEASE)
    list(APPEND ECHO_OVERLAY_IMPORTS
        app_debug_frames
        board_battery_gauge
        board_debug_audio
        board_debug_battery_raw
        board_debug_grf_start
        board_debug_read_grf
        board_debug_interrupted_line
        board_debug_loop
        board_debug_usb_forced
        board_debug_power_history
        board_debug_usb_is_powered
        board_debug_usb_kept_on
        board_power_charge_pin
        board_power_shown
        board_profile
        board_profile_hold
        board_restart
        debug_loop_log
        debug_runner_begin
        debug_runner_script
        hal_clock_microseconds
        hal_internal_file_read
        hal_internal_file_reserve
        hal_internal_file_write
        hal_system_clock_mhz
        hal_system_idle_microseconds
        hal_system_restart_pending
        hal_system_start_info
        hal_system_version
    )
endif()
set(ECHO_DEVICE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/board/echo_device.ld)
set_target_properties(echo_device PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_DEVICE_LINKER_SCRIPT}
)
set_source_files_properties(board/libc.c board/startup.c PROPERTIES
    COMPILE_OPTIONS "-fno-builtin;-fno-tree-loop-distribute-patterns;-fno-lto"
)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(
        board/bluetooth.c
        board/core1.c
        board/dac.c
        board/extension.c
        board/debug_poke.c
        board/debug_report_file.c
        board/debug_rtc.c
        board/debug_script_file.c
        board/debug_session.c
        board/glyphs.c
        board/high_ram.c
        board/language.c
        board/overlay.c
        board/panic.c
        board/profiler.c
        board/start_info.c
        board/system.c
        board/update.c
        board/update_request.c
        board/usb_dac_session.c
        board/usb_listen.c
        board/usb_report.c
        board/usb_session.c
        logic/fat_delete.c
        PROPERTIES COMPILE_OPTIONS -Os
    )
endif()
target_link_libraries(echo_device PRIVATE echo_app echo_device_logic echo_options echo_lto)
list(TRANSFORM ECHO_OVERLAY_IMPORTS PREPEND "-Wl,--require-defined=" OUTPUT_VARIABLE
    ECHO_OVERLAY_IMPORT_OPTIONS)
if(NOT ECHO_RELEASE)
    target_link_options(echo_device PRIVATE
        -Wl,--defsym=device_profile_bucket_shift=${ECHO_PROFILE_BUCKET_SHIFT})
endif()
target_link_options(echo_device PRIVATE
    -flto -flto-partition=one
    ${ECHO_OVERLAY_IMPORT_OPTIONS}
    -T${ECHO_DEVICE_LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_device.map
)

set(ECHO_OVERLAY_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/rescan_overlay.ld)
add_executable(echo_rescan_overlay overlay/rescan_overlay.c)
set_target_properties(echo_rescan_overlay PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS "${ECHO_OVERLAY_LINKER_SCRIPT};$<TARGET_FILE:echo_device>"
)
add_dependencies(echo_rescan_overlay echo_device)
target_link_libraries(echo_rescan_overlay PRIVATE echo_library_scan echo_options gcc)
target_link_options(echo_rescan_overlay PRIVATE
    -fno-lto
    -T${ECHO_OVERLAY_LINKER_SCRIPT}
    -Wl,--just-symbols=$<TARGET_FILE:echo_device>
    -Wl,--allow-multiple-definition
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_rescan_overlay.map
)

set(ECHO_USB_OVERLAY_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/usb_overlay.ld)
add_executable(echo_usb_overlay
    overlay/usb_overlay.c
    board/update.c
    board/usb_dac_session.c
    board/usb_port.c
    board/usb_session.c
    logic/fat_delete.c
)
if(NOT ECHO_RELEASE)
    target_sources(echo_usb_overlay PRIVATE board/debug_report_file.c board/debug_script_file.c)
endif()
set_target_properties(echo_usb_overlay PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS "${ECHO_USB_OVERLAY_LINKER_SCRIPT};$<TARGET_FILE:echo_device>"
)
add_dependencies(echo_usb_overlay echo_device)
target_include_directories(echo_usb_overlay PRIVATE
    board
    logic
    $<TARGET_PROPERTY:echo_app,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:echo_settings,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(echo_usb_overlay PRIVATE echo_device_logic echo_options gcc)
target_link_options(echo_usb_overlay PRIVATE
    -fno-lto
    -T${ECHO_USB_OVERLAY_LINKER_SCRIPT}
    -Wl,--just-symbols=$<TARGET_FILE:echo_device>
    -Wl,--allow-multiple-definition
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_usb_overlay.map
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ECHO_MODULE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/screen_module.ld)
set(ECHO_MODULE_IMPORTS ${CMAKE_CURRENT_BINARY_DIR}/module_imports.rsp)
set(ECHO_MODULE_TARGETS "")
set(ECHO_MODULE_OBJECT_TARGETS "")
set(ECHO_MODULE_IMPORT_ARGUMENTS "")
set(ECHO_MODULE_IMAGE_ARGUMENTS "")
set(ECHO_MODULE_MAP_ARGUMENTS "")
foreach(source IN LISTS ECHO_SCREEN_MODULE_FAST_SOURCES)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/${source} PROPERTIES COMPILE_OPTIONS -O2)
endforeach()
foreach(module IN LISTS ECHO_SCREEN_MODULES)
    if(module STREQUAL "process")
        set(ECHO_MODULE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/audio_process.ld)
    elseif(module STREQUAL "dsp" OR module STREQUAL "ogg")
        set(ECHO_MODULE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/audio_dsp.ld)
    elseif(module STREQUAL "extension")
        set(ECHO_MODULE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/extension_module.ld)
    else()
        set(ECHO_MODULE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/overlay/screen_module.ld)
    endif()
    set(module_target echo_module_${module})
    set(module_sources "")
    set(module_includes "")
    foreach(source IN LISTS ECHO_SCREEN_MODULE_SOURCES_${module})
        echo_screen_module_owner(${source} owner name)
        get_target_property(owner_directory ${owner} SOURCE_DIR)
        list(APPEND module_sources ${PROJECT_SOURCE_DIR}/${source})
        list(APPEND module_includes ${owner_directory} ${owner_directory}/include)
    endforeach()
    list(REMOVE_DUPLICATES module_includes)
    add_library(${module_target}_objects OBJECT ${module_sources})
    if(module STREQUAL "dsp")
        target_link_libraries(${module_target}_objects PRIVATE echo_codec)
        target_compile_definitions(${module_target}_objects PRIVATE
            mp4_probe=audio_dsp_probe
            mp4_cursor_seek=audio_dsp_seek
            mp4_cursor_next=audio_dsp_next
        )
    endif()
    if(module STREQUAL "process")
        target_link_libraries(${module_target}_objects PRIVATE echo_device_logic)
        target_compile_definitions(${module_target}_objects PRIVATE
            equalizer_design=audio_process_equalizer_design
            equalizer_retune=audio_process_equalizer_retune
            equalizer_run=audio_process_equalizer_run
        )
    endif()
    if(module STREQUAL "extension")
        target_link_libraries(${module_target}_objects PRIVATE echo_device_logic)
        target_include_directories(${module_target}_objects PRIVATE board)
        target_compile_definitions(${module_target}_objects PRIVATE
            home_art_draw=extension_home_art_draw
            home_art_draw_backdrop=extension_home_art_draw_backdrop
            home_art_init=extension_home_art_init
            hal_file_remove=extension_hal_file_remove
            home_art_update=extension_home_art_update
            wire_motion_draw=extension_wire_motion_draw
            wire_motion_due=extension_wire_motion_due
            wire_motion_end=extension_wire_motion_end
            wire_motion_fit=extension_wire_motion_fit
            wire_motion_progress=extension_wire_motion_progress
            wire_motion_run=extension_wire_motion_run
            wire_motion_shown=extension_wire_motion_shown
            wire_motion_tick=extension_wire_motion_tick
        )
    endif()
    if(module STREQUAL "ogg")
        target_link_libraries(${module_target}_objects PRIVATE echo_device_logic echo_codec)
        target_compile_definitions(${module_target}_objects PRIVATE
            core1_open_ogg=audio_ogg_open
            ogg_stream_read=audio_ogg_read
        )
    endif()
    target_include_directories(${module_target}_objects PRIVATE ${module_includes})
    target_link_libraries(${module_target}_objects PRIVATE echo_app echo_options)
    if(NOT module STREQUAL "process")
        target_link_libraries(${module_target}_objects PRIVATE echo_compact)
    endif()
    add_executable(${module_target} $<TARGET_OBJECTS:${module_target}_objects>)
    set_target_properties(${module_target} PROPERTIES
        SUFFIX .elf
        LINKER_LANGUAGE C
        LINK_DEPENDS "${ECHO_MODULE_LINKER_SCRIPT};$<TARGET_FILE:echo_device>"
    )
    add_dependencies(${module_target} echo_device)
    target_link_libraries(${module_target} PRIVATE gcc)
    target_link_options(${module_target} PRIVATE
        -fno-lto
        -T${ECHO_MODULE_LINKER_SCRIPT}
        -Wl,--just-symbols=$<TARGET_FILE:echo_device>
        -Wl,--allow-multiple-definition
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${module_target}.map
    )
    list(APPEND ECHO_MODULE_TARGETS ${module_target})
    list(APPEND ECHO_MODULE_OBJECT_TARGETS ${module_target}_objects)
    list(APPEND ECHO_MODULE_IMPORT_ARGUMENTS
        --module ${module} $<TARGET_OBJECTS:${module_target}_objects>)
    list(APPEND ECHO_MODULE_IMAGE_ARGUMENTS --module $<TARGET_FILE:${module_target}>)
    list(APPEND ECHO_MODULE_MAP_ARGUMENTS
        --module-map ${module}=${CMAKE_CURRENT_BINARY_DIR}/${module_target}.map)
endforeach()
add_custom_target(echo_module_imports
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/module_imports.py
        --nm ${CMAKE_NM}
        --output ${ECHO_MODULE_IMPORTS}
        ${ECHO_MODULE_IMPORT_ARGUMENTS}
    BYPRODUCTS ${ECHO_MODULE_IMPORTS}
    COMMAND_EXPAND_LISTS
    VERBATIM
)
add_dependencies(echo_module_imports ${ECHO_MODULE_OBJECT_TARGETS})
add_dependencies(echo_device echo_module_imports)
target_link_options(echo_device PRIVATE -Wl,@${ECHO_MODULE_IMPORTS})
set_property(TARGET echo_device APPEND PROPERTY LINK_DEPENDS ${ECHO_MODULE_IMPORTS})

set(ECHO_DEVICE_IMAGE ${CMAKE_BINARY_DIR}/${FIRMWARE_UPDATE_FILE})
set(ECHO_GLYPH_FACE_ARGUMENTS "")
foreach(face_file ${ECHO_GLYPH_FACE_FILES})
    list(APPEND ECHO_GLYPH_FACE_ARGUMENTS --glyph-face ${face_file})
endforeach()
set(ECHO_LANGUAGE_PACK_ARGUMENTS "")
foreach(pack_file ${ECHO_LANGUAGE_PACK_FILES})
    list(APPEND ECHO_LANGUAGE_PACK_ARGUMENTS --language-pack ${pack_file})
endforeach()

add_custom_target(echo_device_image ALL
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/make_image.py
        --elf $<TARGET_FILE:echo_device>
        --overlay $<TARGET_FILE:echo_rescan_overlay>
        --usb-overlay $<TARGET_FILE:echo_usb_overlay>
        --core1-mp3 ${ECHO_CORE1_MP3_BINARY}
        --core1-flac ${ECHO_CORE1_BINARY}
        --core1-m4a ${ECHO_CORE1_M4A_BINARY}
        --core1-vorbis ${ECHO_CORE1_VORBIS_BINARY}
        --core1-opus ${ECHO_CORE1_OPUS_BINARY}
        ${ECHO_GLYPH_FACE_ARGUMENTS}
        ${ECHO_LANGUAGE_PACK_ARGUMENTS}
        ${ECHO_MODULE_IMAGE_ARGUMENTS}
        --output ${ECHO_DEVICE_IMAGE}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/size_report.py
        --build ${CMAKE_BINARY_DIR} --summary ${ECHO_LANGUAGE_PACK_ARGUMENTS}
        ${ECHO_MODULE_MAP_ARGUMENTS}
    BYPRODUCTS ${ECHO_DEVICE_IMAGE}
    DEPENDS echo_device echo_rescan_overlay echo_usb_overlay ${ECHO_MODULE_TARGETS}
        ${ECHO_CORE1_MP3_BINARY}
        ${ECHO_CORE1_BINARY}
        ${ECHO_CORE1_M4A_BINARY}
        ${ECHO_CORE1_VORBIS_BINARY}
        ${ECHO_CORE1_OPUS_BINARY}
        ${ECHO_LANGUAGE_PACK_FILES}
        ${PROJECT_SOURCE_DIR}/tools/make_image.py
        ${PROJECT_SOURCE_DIR}/tools/identity.py
        ${FIRMWARE_IDENTITY_FILE}
    VERBATIM
)

add_dependencies(echo_device_image echo_language_packs)

add_custom_target(echo_size_report
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/size_report.py
        --build ${CMAKE_BINARY_DIR} ${ECHO_LANGUAGE_PACK_ARGUMENTS} ${ECHO_MODULE_MAP_ARGUMENTS}
    DEPENDS echo_device echo_rescan_overlay echo_usb_overlay ${ECHO_MODULE_TARGETS}
        echo_language_packs
    VERBATIM
    USES_TERMINAL
)
