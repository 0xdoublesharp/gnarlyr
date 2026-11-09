add_executable(echo_device
    board/audio.c
    board/bluetooth.c
    board/clock.c
    board/clock_switch.c
    board/core1.c
    board/cru.c
    board/dac.c
    board/dmac.c
    board/display.c
    board/emmc.c
    board/glyphs.c
    board/gpio.c
    board/jack.c
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
    board/storage_runtime.c
    board/system.c
    board/update.c
    board/usb.c
    board/usb_dac_session.c
    board/usb_report.c
    board/usb_session.c
)
target_sources(echo_device PRIVATE board/panel_vop.c)
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
set(ECHO_CORE1_BINARY ${CMAKE_CURRENT_BINARY_DIR}/core1.bin)
add_custom_command(
    OUTPUT ${ECHO_CORE1_BINARY}
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:echo_core1> core1.bin
    DEPENDS echo_core1
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

set(ECHO_DEVICE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/board/echo_device.ld)
set_target_properties(echo_device PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS ${ECHO_DEVICE_LINKER_SCRIPT}
)
set_source_files_properties(board/libc.c board/startup.c PROPERTIES
    COMPILE_OPTIONS "-fno-builtin;-fno-tree-loop-distribute-patterns"
)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(
        board/bluetooth.c
        board/core1.c
        board/dac.c
        board/glyphs.c
        board/high_ram.c
        board/overlay.c
        board/panic.c
        board/start_info.c
        board/system.c
        board/storage_runtime.c
        board/update.c
        board/usb_dac_session.c
        board/usb_report.c
        board/usb_session.c
        PROPERTIES COMPILE_OPTIONS -Os
    )
endif()
target_link_libraries(echo_device PRIVATE echo_app echo_device_logic echo_options)
target_compile_definitions(echo_device PRIVATE PODIUM_VERSION_TEXT="${PODIUM_VERSION}")
target_link_options(echo_device PRIVATE
    -Wl,--undefined=hal_file_remove
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
    -T${ECHO_OVERLAY_LINKER_SCRIPT}
    -Wl,--just-symbols=$<TARGET_FILE:echo_device>
    -Wl,--allow-multiple-definition
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_rescan_overlay.map
)

set(ECHO_STORAGE_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/resident/storage_code.ld)
add_executable(echo_storage_code
    resident/storage_entry.c
    ${PROJECT_SOURCE_DIR}/src/app/mixtape_store.c
    ${PROJECT_SOURCE_DIR}/src/app/mixtape_ui.c
    ${PROJECT_SOURCE_DIR}/src/app/mixtape_screen.c
    ${PROJECT_SOURCE_DIR}/src/playback/mix_api.c
    ${PROJECT_SOURCE_DIR}/src/playback/mix_catalog.c
    ${PROJECT_SOURCE_DIR}/src/playback/mix_identity.c
    ${PROJECT_SOURCE_DIR}/src/playback/mix_tape.c
    ${PROJECT_SOURCE_DIR}/src/playback/mix_resolve.c
    ${PROJECT_SOURCE_DIR}/src/playback/track_order.c
    ${PROJECT_SOURCE_DIR}/src/playback/session_journal.c
)
set_target_properties(echo_storage_code PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS "${ECHO_STORAGE_LINKER_SCRIPT};$<TARGET_FILE:echo_device>"
)
add_dependencies(echo_storage_code echo_device)
target_include_directories(echo_storage_code PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(echo_storage_code PRIVATE echo_app echo_playback echo_device_logic echo_options echo_compact gcc)
target_compile_definitions(echo_storage_code PRIVATE
    mix_code_api=resident_mix_code_api
    mix_identity_valid=resident_mix_identity_valid
    mix_identity_encode=resident_mix_identity_encode
    mix_catalog_open=resident_mix_catalog_open
    mix_catalog_previous=resident_mix_catalog_previous
    mix_catalog_read=resident_mix_catalog_read
    mix_catalog_begin=resident_mix_catalog_begin
    mix_catalog_step=resident_mix_catalog_step
    mix_catalog_abort=resident_mix_catalog_abort
    mix_identity_read=resident_mix_identity_read
    mix_identity_equal=resident_mix_identity_equal
    mix_identity_hash=resident_mix_identity_hash
    mix_measure_begin=resident_mix_measure_begin
    mix_measure_step=resident_mix_measure_step
    mix_tape_write_begin=resident_mix_tape_write_begin
    mix_tape_write_step=resident_mix_tape_write_step
    mix_tape_write_abort=resident_mix_tape_write_abort
    mix_tape_check_begin=resident_mix_tape_check_begin
    mix_tape_check_step=resident_mix_tape_check_step
    mix_tape_cursor=resident_mix_tape_cursor
    mix_tape_next=resident_mix_tape_next
    mix_resolve_begin=resident_mix_resolve_begin
    mix_resolve_step=resident_mix_resolve_step
    mix_resolve_source=resident_mix_resolve_source
    track_order_open=resident_track_order_open
    track_order_open_at=resident_track_order_open_at
    track_order_read=resident_track_order_read
    track_order_source=resident_track_order_source
    track_order_job_begin=resident_track_order_job_begin
    track_order_job_step=resident_track_order_job_step
    track_order_job_abort=resident_track_order_job_abort
    track_order_job_progress=resident_track_order_job_progress
    session_journal_open=resident_session_journal_open
    session_journal_previous=resident_session_journal_previous
    session_journal_save=resident_session_journal_save
)
target_link_options(echo_storage_code PRIVATE
    -T${ECHO_STORAGE_LINKER_SCRIPT}
    -Wl,--just-symbols=$<TARGET_FILE:echo_device>
    -Wl,--allow-multiple-definition
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/echo_storage_code.map
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(ECHO_FIRMWARE_VERSION ${PODIUM_VERSION})
set(ECHO_DEVICE_IMAGE ${CMAKE_BINARY_DIR}/${PODIUM_IMAGE_NAME})
set(ECHO_IMAGE_BRAND PODIUM)
set(ECHO_GLYPH_FACE_ARGUMENTS "")
foreach(face_file ${ECHO_GLYPH_FACE_FILES})
    list(APPEND ECHO_GLYPH_FACE_ARGUMENTS --glyph-face ${face_file})
endforeach()

add_custom_target(echo_device_image ALL
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/make_image.py
        --version ${ECHO_FIRMWARE_VERSION}
        --brand ${ECHO_IMAGE_BRAND}
        --elf $<TARGET_FILE:echo_device>
        --overlay $<TARGET_FILE:echo_rescan_overlay>
        --storage-code $<TARGET_FILE:echo_storage_code>
        --core1-mp3 ${ECHO_CORE1_MP3_BINARY}
        --core1-flac ${ECHO_CORE1_BINARY}
        --core1-m4a ${ECHO_CORE1_M4A_BINARY}
        ${ECHO_GLYPH_FACE_ARGUMENTS}
        --output ${ECHO_DEVICE_IMAGE}
    BYPRODUCTS ${ECHO_DEVICE_IMAGE}
    DEPENDS echo_device echo_rescan_overlay echo_storage_code ${ECHO_CORE1_MP3_BINARY} ${ECHO_CORE1_BINARY}
        ${ECHO_CORE1_M4A_BINARY}
        ${PROJECT_SOURCE_DIR}/tools/make_image.py
    VERBATIM
)
