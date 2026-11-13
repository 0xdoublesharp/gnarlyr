add_library(echo_check STATIC support/check.c)
target_include_directories(echo_check PUBLIC support)
target_link_libraries(echo_check PRIVATE echo_options)

add_library(echo_test_pack STATIC support/test_pack.c)
target_include_directories(echo_test_pack PUBLIC support)
target_link_libraries(echo_test_pack PUBLIC echo_core PRIVATE echo_options)

echo_add_language_pack(${ECHO_TEST_LANGUAGE} ECHO_PSEUDO_PACK)
list(GET ECHO_LANGUAGE_PACK_FILES 0 ECHO_ENGLISH_PACK)
add_custom_target(echo_test_language_packs DEPENDS ${ECHO_PSEUDO_PACK})
add_dependencies(echo_test_language_packs echo_language_packs)

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

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/core.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/gfx.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/library_fixtures.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/playback.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/settings.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/library.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/ui.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/app.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/device.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/codec.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/audio.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/image.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/storage.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/sim.cmake)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME card_format_table_up_to_date
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/card_format_tables.py
        --output ${PROJECT_SOURCE_DIR}/platform/device/logic/card_format_upcase.h --check
)

find_program(FSCK_MSDOS fsck_msdos)
find_program(FSCK_EXFAT fsck_exfat)
if(FSCK_MSDOS AND FSCK_EXFAT)
    add_test(NAME formatted_cards_pass_fsck
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_formatted_cards.py
            --tool $<TARGET_FILE:test_card_format>
            --directory ${CMAKE_CURRENT_BINARY_DIR}/formatted_cards
    )
    set_tests_properties(formatted_cards_pass_fsck PROPERTIES RESOURCE_LOCK disk_images TIMEOUT 180)
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
add_test(NAME no_text_literals
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_literals.py ${PROJECT_SOURCE_DIR}
)
add_test(NAME no_identity_literals
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_identity_literals.py
        ${PROJECT_SOURCE_DIR}
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
    foreach(module IN LISTS ECHO_SCREEN_MODULES)
        foreach(source IN LISTS ECHO_SCREEN_MODULE_SOURCES_${module})
            string(REGEX REPLACE "^src/" "" audit_source ${source})
            list(APPEND target_audit_definitions --module-source ${module}=${audit_source})
        endforeach()
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
    set_tests_properties(device_build PROPERTIES
        FIXTURES_SETUP device_image
        RESOURCE_LOCK firmware_images
    )
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
        file(GLOB echo_image_scenarios CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/emulator/scenarios/test_*.py)
        list(SORT echo_image_scenarios)
        foreach(echo_image_tests IN LISTS echo_image_scenarios)
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
        endforeach()
        set_tests_properties(
            device_image_boot_test_parametric_slot_matches_graphic_core_load_and_switches_without_underruns
            PROPERTIES TIMEOUT 1500
        )
        add_test(NAME core1_mp3_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_mp3.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_mp3.bin
                --tool $<TARGET_FILE:mp3_decode_tool>
        )
        set_tests_properties(core1_mp3_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            SKIP_RETURN_CODE 77
            TIMEOUT 180
        )
        add_test(NAME core1_vorbis_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_vorbis.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_vorbis.bin
                --tool $<TARGET_FILE:ogg_packet_tool>
        )
        set_tests_properties(core1_vorbis_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            SKIP_RETURN_CODE 77
            TIMEOUT 180
        )
        add_test(NAME core1_opus_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_opus.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_opus.bin
                --tool $<TARGET_FILE:ogg_packet_tool>
        )
        set_tests_properties(core1_opus_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )
        add_test(NAME core1_dsd_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_dsd.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_dsd.bin
                --tool $<TARGET_FILE:dsd_stream_tool>
        )
        set_tests_properties(core1_dsd_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            TIMEOUT 600
        )
        add_test(NAME core1_wma_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_wma.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_wma.bin
                --tool $<TARGET_FILE:wma_decode_tool>
                --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/wma
        )
        set_tests_properties(core1_wma_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            TIMEOUT 600
        )
        add_test(NAME core1_ape_decodes_in_emulator
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_ape.py
                --program ${PROJECT_SOURCE_DIR}/build/device/platform/device/core1_ape.bin
                --tool $<TARGET_FILE:ape_stream_tool>
                --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ape
        )
        set_tests_properties(core1_ape_decodes_in_emulator PROPERTIES
            FIXTURES_REQUIRED device_image
            TIMEOUT 600
        )
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
    add_test(NAME battery_tool_tests
        COMMAND ${ECHO_TOOLS_PYTHON} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools/tests
            -p test_battery.py
    )
    add_test(NAME tools_unit_tests
        COMMAND ${ECHO_TOOLS_PYTHON} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools/tests
    )
    set_tests_properties(tools_unit_tests PROPERTIES RESOURCE_LOCK firmware_images)
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
    add_test(NAME strings_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lang_pack.py
            --languages ${PROJECT_SOURCE_DIR}/assets/lang
            --sources ${PROJECT_SOURCE_DIR}/src/core
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
    foreach(book_fixture m4b_music m4b_real)
        add_test(NAME ${book_fixture}_up_to_date
            COMMAND ${CMAKE_COMMAND}
                -DPYTHON=${ECHO_TOOLS_PYTHON}
                -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
                -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${book_fixture}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${book_fixture}.db
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${book_fixture}.db
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
        )
    endforeach()
    add_test(NAME replay_gain_table_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/replay_gain_table.py
            --output ${PROJECT_SOURCE_DIR}/src/library/replay_gain_table.h
            --style ${PROJECT_SOURCE_DIR}/.clang-format
            --check
    )
    add_test(NAME audio_resample_table_up_to_date
        COMMAND ${ECHO_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/audio_resample_table.py
            --output ${PROJECT_SOURCE_DIR}/platform/device/logic/audio_resample_table.h
            --style ${PROJECT_SOURCE_DIR}/.clang-format
            --check
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

include(${CMAKE_CURRENT_SOURCE_DIR}/registration/app_features.cmake)
