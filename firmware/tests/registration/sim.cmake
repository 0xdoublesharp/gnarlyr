if(ECHO_BUILD_SIM)
    target_link_libraries(test_usb_screen PRIVATE echo_sim_glyphs)
    target_compile_definitions(test_usb_screen PRIVATE ECHO_TEST_GLYPHS)
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
        LIBRARIES echo_sim_storage echo_playback
    )
    echo_add_unit_test(NAME test_sim_glyphs
        SOURCES sim/test_sim_glyphs.c
        LIBRARIES echo_sim_glyphs echo_fonts echo_test_pack
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
    add_test(NAME replay_gain_cache_matches_desktop
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library/check_replay_gain_cache.py
            --builder $<TARGET_FILE:test_library_replay_gain>
            --scanner $<TARGET_FILE:test_library_scan>
            --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags
    )
    add_test(NAME audiobook_indexers_agree
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_audiobook_index.py
            --scanner $<TARGET_FILE:test_library_scan>
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
            --keys play,down,down,down,down,play,down,play,play,down,play
            --screenshot ${CMAKE_BINARY_DIR}/sim_cue_album.bmp
    )
endif()
