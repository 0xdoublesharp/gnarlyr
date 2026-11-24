include(${MODULE})

function(scaled_by factor seconds output)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ECHO_EMULATOR_TIMEOUT_SCALE=${factor}
            ${CMAKE_COMMAND} -DMODE=one -DSECONDS=${seconds} -DMODULE=${MODULE} -P ${CMAKE_CURRENT_LIST_FILE}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE value
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT result EQUAL 0)
        set(value refused)
    endif()
    set(${output} ${value} PARENT_SCOPE)
endfunction()

function(expect factor seconds wanted)
    scaled_by("${factor}" ${seconds} value)
    if(NOT value STREQUAL wanted)
        message(FATAL_ERROR "scale '${factor}' on ${seconds} s gave ${value}, expected ${wanted}")
    endif()
endfunction()

function(registered_timeout tests name output)
    string(JSON count LENGTH "${tests}")
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON test_name GET "${tests}" ${index} name)
        if(test_name STREQUAL name)
            string(JSON properties GET "${tests}" ${index} properties)
            string(JSON property_count LENGTH "${properties}")
            math(EXPR last_property "${property_count} - 1")
            foreach(property RANGE ${last_property})
                string(JSON property_name GET "${properties}" ${property} name)
                if(property_name STREQUAL "TIMEOUT")
                    string(JSON value GET "${properties}" ${property} value)
                    set(${output} ${value} PARENT_SCOPE)
                    return()
                endif()
            endforeach()
        endif()
    endforeach()
    message(FATAL_ERROR "${name} is not registered with a timeout")
endfunction()

if(MODE STREQUAL "one")
    echo_emulator_timeout(value ${SECONDS})
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo ${value})
elseif(MODE STREQUAL "arithmetic")
    expect("" 900 900)
    expect(1 1800 1800)
    expect(2.5 1800 4500)
    expect(2.5 900 2250)
    expect(1.33 900 1197)
    expect(3 180 540)
    expect(1.05 7 8)
    expect(0.5 900 refused)
    expect(abc 900 refused)
    expect(2.555 900 refused)
elseif(MODE STREQUAL "registered")
    set(cases
        "device_image_boot_test_dsd128_resume_at_0_ms_in_core_1s_load_window_stays_fed=900"
        "device_image_boot_test_parametric_slot_matches_graphic_core_load_and_switches_without_underruns=1500"
        "device_image_boot_test_normal_boot_reaches_the_home_screen_using_only_the_user_volume=900"
        "emulator_snapshot_parity_native=900"
        "emulator_snapshot_parity_python=900"
        "core1_mp3_decodes_in_emulator=180"
        "core1_opus_decodes_in_emulator=600")
    execute_process(
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${BUILD_DIR} --show-only=json-v1
            -R "^(device_image_boot_test_dsd128_resume_at_0_ms|device_image_boot_test_parametric_slot|device_image_boot_test_normal_boot_reaches|emulator_snapshot_parity_|core1_(mp3|opus)_decodes_in_emulator)"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE listing
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "ctest could not list the emulator tests")
    endif()
    string(JSON tests GET "${listing}" tests)
    foreach(case IN LISTS cases)
        string(REPLACE "=" ";" parts "${case}")
        list(GET parts 0 name)
        list(GET parts 1 base)
        math(EXPR wanted "(${base} * ${HUNDREDTHS} + 99) / 100")
        registered_timeout("${tests}" ${name} value)
        if(NOT value EQUAL wanted)
            message(FATAL_ERROR "${name} has TIMEOUT ${value}, expected ${wanted} at scale ${HUNDREDTHS}/100")
        endif()
    endforeach()
else()
    message(FATAL_ERROR "MODE must be one, arithmetic or registered")
endif()
