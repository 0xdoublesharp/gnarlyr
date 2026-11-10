add_library(echo_check STATIC support/check.c)
target_include_directories(echo_check PUBLIC support)
target_link_libraries(echo_check PRIVATE echo_options)

function(echo_add_unit_test)
    cmake_parse_arguments(TEST "" "NAME" "SOURCES;LIBRARIES;PRIVATE_INCLUDES" ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    if(WIN32)
        target_sources(${TEST_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/cmake/windows_utf8.rc)
    endif()
    target_link_libraries(${TEST_NAME} PRIVATE echo_check echo_options ${TEST_LIBRARIES})
    target_include_directories(${TEST_NAME} PRIVATE ${TEST_PRIVATE_INCLUDES})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

echo_add_unit_test(NAME test_deadline SOURCES core/test_deadline.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_frame_pacer SOURCES core/test_frame_pacer.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_frame_stats SOURCES core/test_frame_stats.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_key_tracker SOURCES core/test_key_tracker.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_key_lock SOURCES core/test_key_lock.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_utf8 SOURCES core/test_utf8.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_string_builder SOURCES core/test_string_builder.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_format SOURCES core/test_format.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_dac_identity SOURCES core/test_dac_identity.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_random SOURCES core/test_random.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_permutation SOURCES core/test_permutation.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_hash SOURCES core/test_hash.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_power_policy SOURCES core/test_power_policy.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_low_battery SOURCES core/test_low_battery.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_key_script SOURCES core/test_key_script.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_debug_log SOURCES core/test_debug_log.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_color SOURCES gfx/test_color.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_rect SOURCES gfx/test_rect.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_canvas SOURCES gfx/test_canvas.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_image SOURCES gfx/test_image.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_strip_renderer SOURCES gfx/test_strip_renderer.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_frame_diff SOURCES gfx/test_frame_diff.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_font SOURCES gfx/test_font.c gfx/tiny_font.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_text SOURCES gfx/test_text.c gfx/tiny_font.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_font_pack SOURCES gfx/test_font_pack.c gfx/tiny_font.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_fonts SOURCES fonts/test_fonts.c LIBRARIES echo_fonts)

add_library(echo_library_fixtures STATIC
    library/memory_source.c
    library/fixture_library.c
    library/big_library.c
    library/spoken_library.c
)
target_include_directories(echo_library_fixtures PUBLIC library)
target_compile_definitions(echo_library_fixtures PRIVATE
    ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(echo_library_fixtures PUBLIC echo_library echo_library_scan PRIVATE echo_options)

foreach(playback_test play_queue resume_state up_next bookmarks spoken store track_order session_journal)
    echo_add_unit_test(NAME test_${playback_test}
        SOURCES playback/test_${playback_test}.c
        LIBRARIES echo_playback echo_library_fixtures
    )
endforeach()

echo_add_unit_test(NAME test_settings SOURCES settings/test_settings.c LIBRARIES echo_settings)
echo_add_unit_test(NAME test_rocker SOURCES settings/test_rocker.c LIBRARIES echo_settings)
echo_add_unit_test(NAME test_equalizer_fit SOURCES settings/test_equalizer_fit.c LIBRARIES echo_settings)
echo_add_unit_test(NAME test_equalizer_slots SOURCES settings/test_equalizer_slots.c LIBRARIES echo_settings)

foreach(library_test block_cache library library_content library_browse favorites covers sort_key track_fields playlist_reader cue_sheet music_fingerprint)
    echo_add_unit_test(NAME test_${library_test}
        SOURCES library/test_${library_test}.c
        LIBRARIES echo_library_fixtures
    )
endforeach()
add_library(echo_reference_builder STATIC
    library/reference/reference_builder.c
    library/reference/reference_builder_encode.c
    library/reference/reference_builder_model.c
    library/reference/reference_builder_paths.c
)
target_include_directories(echo_reference_builder PUBLIC library/reference)
target_link_libraries(echo_reference_builder PUBLIC echo_library_scan PRIVATE echo_options)

add_library(echo_library_checks STATIC library/library_compare.c library/random_library.c)
target_include_directories(echo_library_checks PUBLIC library)
target_link_libraries(echo_library_checks PUBLIC echo_library_scan PRIVATE echo_options)

echo_add_unit_test(NAME test_library_builder
    SOURCES library/test_library_builder.c
    LIBRARIES echo_library_fixtures echo_library_checks echo_reference_builder
)
set_tests_properties(test_library_builder PROPERTIES TIMEOUT 600)
target_compile_definitions(test_library_builder PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_track_tags
    SOURCES library/test_track_tags.c
    LIBRARIES echo_library_fixtures
)
target_compile_definitions(test_track_tags PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_cover_build
    SOURCES library/test_cover_build.c
    LIBRARIES echo_library_scan
)
target_compile_definitions(test_cover_build PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

echo_add_unit_test(NAME test_screen_stack SOURCES ui/test_screen_stack.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_navigation SOURCES ui/test_navigation.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_marquee SOURCES ui/test_marquee.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_fade SOURCES ui/test_fade.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_slide SOURCES ui/test_slide.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_cover_flow SOURCES ui/test_cover_flow.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_label
    SOURCES ui/test_label.c gfx/tiny_font.c
    LIBRARIES echo_ui
    PRIVATE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/gfx
)

add_library(echo_ui_fixtures STATIC gfx/tiny_font.c ui/ui_test_theme.c ui/ui_test_canvas.c)
target_include_directories(echo_ui_fixtures PUBLIC gfx ui)
target_link_libraries(echo_ui_fixtures PUBLIC echo_ui PRIVATE echo_options)

foreach(ui_test theme_layout list_view list_navigation fast_scroll header menu_screen message_screen
        choice_screen toast status_bar)
    echo_add_unit_test(NAME test_${ui_test}
        SOURCES ui/test_${ui_test}.c
        LIBRARIES echo_ui_fixtures
    )
endforeach()

echo_add_unit_test(NAME test_app
    SOURCES app/test_app.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_release_diagnostics
    SOURCES app/test_release_diagnostics.c ${PROJECT_SOURCE_DIR}/src/app/diagnostics_menu_release.c
        app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_draw_profile
    SOURCES app/test_draw_profile.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_player
    SOURCES app/test_player.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_player_cue
    SOURCES app/test_player_cue.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_spoken_word
    SOURCES app/test_spoken_word.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_cover_cache
    SOURCES app/test_cover_cache.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_stores
    SOURCES app/test_stores.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_browse_screen
    SOURCES app/test_browse_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_up_next_screen
    SOURCES app/test_up_next_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_cover_flow_order
    SOURCES app/test_cover_flow_order.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_board_layout
    SOURCES app/test_board_layout.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_now_playing_screen
    SOURCES app/test_now_playing_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_diagnostics_values
    SOURCES app/test_diagnostics_values.c app/fake_hal.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_bluetooth_link
    SOURCES app/test_bluetooth_link.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_bluetooth_screen
    SOURCES app/test_bluetooth_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
foreach(device_test audio_math backlight board_pins loader_request update_names updater_echo vop battery_gauge watchdog panic_text stall_watch bt_module card_write core1_control start_record dac_sequence dma_loop flac_accel headset_dips headset_monitor headset_remote jack_sense key_ladder key_scanner msc panel pll sd_card stream_drift tick_clock updater usb_audio usb_audio_sink usb_device usb_fallback wav_source pc_profile debug_report power_history)
    echo_add_unit_test(NAME test_${device_test}
        SOURCES device/test_${device_test}.c
        LIBRARIES echo_device_logic
    )
endforeach()

echo_add_unit_test(NAME test_emmc_layout
    SOURCES device/test_emmc_layout.c device/fake_disk.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_storage_mount
    SOURCES device/test_storage_mount.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_storage_mount PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_alac
    SOURCES codec/test_alac.c
    LIBRARIES echo_codec
)
target_compile_definitions(test_alac PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_flac
    SOURCES codec/test_flac.c
    LIBRARIES echo_codec
)
target_compile_definitions(test_flac PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_flac_bits
    SOURCES codec/test_flac_bits.c
    LIBRARIES echo_codec
)
target_include_directories(test_flac_bits PRIVATE ${PROJECT_SOURCE_DIR}/src/codec)
echo_add_unit_test(NAME test_equalizer
    SOURCES device/test_equalizer.c
    LIBRARIES echo_device_logic echo_settings
)
echo_add_unit_test(NAME test_audio_engine
    SOURCES device/test_audio_engine.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_audio_engine PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_mp3_probe
    SOURCES device/test_mp3_probe.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_mp3_probe PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_mp3_decoder
    SOURCES device/test_mp3_decoder.c
    LIBRARIES echo_device_logic echo_libmad
)
target_compile_definitions(test_mp3_decoder PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_mp4_demux
    SOURCES device/test_mp4_demux.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_mp4_demux PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_mp4_demux PRIVATE device)
add_executable(m4a_stream_tool device/m4a_stream_tool.c device/m4a_fixture_files.c)
target_link_libraries(m4a_stream_tool PRIVATE echo_device_logic echo_options)
target_include_directories(m4a_stream_tool PRIVATE device)
target_compile_definitions(m4a_stream_tool PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_m4a_source
    SOURCES device/test_m4a_source.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_m4a_source PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_m4a_source PRIVATE device)
echo_add_unit_test(NAME test_core1_stream
    SOURCES device/test_core1_stream.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_core1_stream PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_cover_grid
    SOURCES image/test_cover_grid.c
    LIBRARIES echo_image
)
add_executable(cover_decode_tool image/cover_decode_tool.c)
target_link_libraries(cover_decode_tool PRIVATE echo_image echo_options)
echo_add_unit_test(NAME test_fat
    SOURCES device/test_fat.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_fat PROPERTIES TIMEOUT 20)
echo_add_unit_test(NAME test_fat_extents
    SOURCES device/test_fat_extents.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat_extents PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_fat_extents PROPERTIES TIMEOUT 20)
echo_add_unit_test(NAME test_stored_queue
    SOURCES device/test_stored_queue.c device/sparse_card.c
    LIBRARIES echo_device_logic echo_playback echo_library_fixtures
)
target_compile_definitions(test_stored_queue PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_stored_queue PROPERTIES TIMEOUT 30)
set(ECHO_CREATED_CARDS ${CMAKE_CURRENT_BINARY_DIR}/created_cards)
file(MAKE_DIRECTORY ${ECHO_CREATED_CARDS})
echo_add_unit_test(NAME test_fat_create
    SOURCES device/test_fat_create.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat_create PRIVATE
    ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ECHO_CREATED_DIR="${ECHO_CREATED_CARDS}"
)
set_tests_properties(test_fat_create PROPERTIES TIMEOUT 30 FIXTURES_SETUP created_cards)
echo_add_unit_test(NAME test_block_read SOURCES device/test_block_read.c LIBRARIES echo_device_logic)
echo_add_unit_test(NAME test_fat_delete
    SOURCES device/test_fat_delete.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat_delete PRIVATE
    ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ECHO_CREATED_DIR="${ECHO_CREATED_CARDS}"
)
set_tests_properties(test_fat_delete PROPERTIES TIMEOUT 30 FIXTURES_SETUP created_cards)

if(ECHO_BUILD_SIM)
    echo_add_unit_test(NAME test_sim_options
        SOURCES sim/test_sim_options.c
        LIBRARIES echo_sim_options
    )
    echo_add_unit_test(NAME test_sim_script
        SOURCES sim/test_sim_script.c
        LIBRARIES echo_sim_options
    )
    echo_add_unit_test(NAME test_sim_decoder
        SOURCES sim/test_sim_decoder.c
        LIBRARIES echo_sim_decoder
    )
    target_compile_definitions(test_sim_decoder PRIVATE
        ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    echo_add_unit_test(NAME test_sim_storage
        SOURCES sim/test_sim_storage.c
        LIBRARIES echo_sim_storage
    )
    echo_add_unit_test(NAME test_sim_glyphs
        SOURCES sim/test_sim_glyphs.c
        LIBRARIES echo_sim_glyphs echo_fonts
    )
    target_compile_definitions(test_sim_storage PRIVATE
        ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        ECHO_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
    echo_add_unit_test(NAME test_library_scan
        SOURCES library/test_library_scan.c
        LIBRARIES echo_library_scan echo_sim_storage echo_library_checks
    )
    target_compile_definitions(test_library_scan PRIVATE
        ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    add_executable(cover_build_tool library/cover_build_tool.c)
    target_link_libraries(cover_build_tool PRIVATE echo_library_scan echo_sim_storage
        echo_options)
    if(EXISTS ${PROJECT_SOURCE_DIR}/.venv/bin/python)
        add_test(NAME cover_build_matches_reference
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${PROJECT_SOURCE_DIR}/tools/check_cover_build.py
                --tool $<TARGET_FILE:cover_build_tool>
                --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
                --work ${CMAKE_CURRENT_BINARY_DIR}/cover_build
        )
        set_tests_properties(cover_build_matches_reference PROPERTIES TIMEOUT 600)
    endif()
    add_test(NAME sim_headless_smoke
        COMMAND echo_sim --headless --frames 30 --screenshot ${CMAKE_BINARY_DIR}/sim_smoke.bmp
    )
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cue_music.db
        ${CMAKE_BINARY_DIR}/sim_cue_system/library.db COPYONLY)
    add_test(NAME sim_shows_a_cue_album_with_its_tracks
        COMMAND echo_sim --headless --frames 400
            --card ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cue_music
            --system-dir ${CMAKE_BINARY_DIR}/sim_cue_system
            --keys play,down,down,down,play,down,play,play,down,play
            --screenshot ${CMAKE_BINARY_DIR}/sim_cue_album.bmp
    )
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_program(FSCK_MSDOS fsck_msdos)
find_program(FSCK_EXFAT fsck_exfat)
if(FSCK_MSDOS AND FSCK_EXFAT)
    add_test(NAME created_cards_pass_fsck
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_card_images.py
            ${ECHO_CREATED_CARDS} --expect 7
    )
    set_tests_properties(created_cards_pass_fsck PROPERTIES
        FIXTURES_REQUIRED created_cards
        RESOURCE_LOCK disk_images
        TIMEOUT 120
    )
else()
    message(STATUS "fsck_msdos or fsck_exfat not found: created_cards_pass_fsck is skipped")
endif()

add_test(NAME no_comments
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_no_comments.py ${PROJECT_SOURCE_DIR}
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMATTED_SOURCES CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.h
        ${PROJECT_SOURCE_DIR}/platform/*.c ${PROJECT_SOURCE_DIR}/platform/*.h
        ${PROJECT_SOURCE_DIR}/tests/*.c ${PROJECT_SOURCE_DIR}/tests/*.h
    )
    add_test(NAME format COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMATTED_SOURCES})
else()
    message(STATUS "clang-format not found: the format test is skipped")
endif()

find_program(ARM_GCC arm-none-eabi-gcc)
if(ARM_GCC)
    set(target_audit_definitions --define ECHO_FONTS_PACKED --define ECHO_DEBUG_LOOP)
    foreach(definition IN LISTS FIRMWARE_DEFINITIONS)
        list(APPEND target_audit_definitions --define ${definition})
    endforeach()
    add_test(NAME target_audit
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/target_audit.py
            --source-root ${PROJECT_SOURCE_DIR}/src
            --budget-bytes ${ECHO_APP_RAM_BUDGET_BYTES}
            ${target_audit_definitions}
    )
    add_test(NAME device_build
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DPRESET=device
            -P ${PROJECT_SOURCE_DIR}/cmake/check_device_build.cmake
    )
    set_tests_properties(device_build PROPERTIES FIXTURES_SETUP device_image)
    add_executable(check_device_image device/check_device_image.c)
    target_link_libraries(check_device_image PRIVATE echo_device_logic echo_options)
    add_test(NAME device_image_passes_the_updater_check
        COMMAND check_device_image ${PROJECT_SOURCE_DIR}/build/device/${FIRMWARE_UPDATE_FILE}
    )
    set_tests_properties(device_image_passes_the_updater_check PROPERTIES
        FIXTURES_REQUIRED device_image
        SKIP_RETURN_CODE 77
    )
    if(EXISTS ${PROJECT_SOURCE_DIR}/.venv/bin/python)
        set(echo_image_tests ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_echo_image.py)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${echo_image_tests})
        file(STRINGS ${echo_image_tests} echo_image_cases REGEX "^    def test_")
        foreach(case_line IN LISTS echo_image_cases)
            string(REGEX REPLACE "^    def (test_[A-Za-z0-9_]+).*" "\\1" case_name "${case_line}")
            set(test_name device_image_boot_${case_name})
            add_test(NAME ${test_name}
                COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python ${echo_image_tests}
                    --image ${PROJECT_SOURCE_DIR}/build/device/${FIRMWARE_UPDATE_FILE}
                    --elf ${PROJECT_SOURCE_DIR}/build/device/platform/device/echo_device.elf
                    --usb-overlay-elf
                    ${PROJECT_SOURCE_DIR}/build/device/platform/device/echo_usb_overlay.elf
                    EchoImageTest.${case_name}
            )
            set_tests_properties(${test_name} PROPERTIES
                FIXTURES_REQUIRED device_image
                SKIP_RETURN_CODE 77
                LABELS echo_image
                PROCESSORS 2
                TIMEOUT 900
            )
        endforeach()
        add_test(NAME core1_m4a_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_m4a.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_m4a.bin
                --tool $<TARGET_FILE:m4a_stream_tool>
        )
        set_tests_properties(core1_m4a_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            SKIP_RETURN_CODE 77
            TIMEOUT 300
        )
    endif()
else()
    message(STATUS "arm-none-eabi-gcc not found: the target_audit test is skipped")
endif()

set(ECHO_TOOLS_PYTHON ${PROJECT_SOURCE_DIR}/.venv/bin/python)
if(EXISTS ${ECHO_TOOLS_PYTHON})
    add_test(NAME tools_unit_tests
        COMMAND ${ECHO_TOOLS_PYTHON} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools/tests
    )
    add_test(NAME cover_decode_matches_pillow
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/check_cover_decode.py
            --tool $<TARGET_FILE:cover_decode_tool>
            --work ${CMAKE_CURRENT_BINARY_DIR}/cover_decode
    )
    set_tests_properties(cover_decode_matches_pillow PROPERTIES TIMEOUT 1200)
    add_test(NAME fonts_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/font_build.py
            --config ${PROJECT_SOURCE_DIR}/assets/fonts/fonts.json
            --output ${PROJECT_SOURCE_DIR}/src/fonts
            --style ${PROJECT_SOURCE_DIR}/.clang-format
            --check
    )
    add_test(NAME glyph_packs_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/glyph_pack.py
            --config ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json
            --output ${PROJECT_SOURCE_DIR}/src/fonts/packs
            --check
    )
    if(APPLE AND EXISTS /opt/local/lib/libfribidi.0.dylib)
        set_tests_properties(fonts_up_to_date glyph_packs_up_to_date PROPERTIES
            ENVIRONMENT DYLD_FALLBACK_LIBRARY_PATH=/opt/local/lib)
    endif()
    add_test(NAME cover_fixture_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/cover_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small_covers.bin
            --check
    )
    add_test(NAME tags_library_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${ECHO_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags_library.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tags_library.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME scan_music_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${ECHO_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scan_music.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME cue_music_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${ECHO_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cue_music
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cue_music.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cue_music.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME m4a_music_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${ECHO_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/m4a_music
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/m4a_music.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/m4a_music.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME m4b_fixture_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/m4b_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/m4b_music
            --check
    )
    add_test(NAME builder_fixture_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/builder_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/builder_random
            --check
    )
    add_test(NAME sort_fold_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/fold_table.py
            --output ${PROJECT_SOURCE_DIR}/src/library
            --style ${PROJECT_SOURCE_DIR}/.clang-format
            --check
    )
    add_test(NAME alac_fixture_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/alac_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/alac
            --check
    )
    add_test(NAME library_fixture_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/library_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small_library.db
            --tracks-only-output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small_library_tracks.db
            --check
    )
else()
    message(STATUS "tools venv not found: run python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt")
endif()
