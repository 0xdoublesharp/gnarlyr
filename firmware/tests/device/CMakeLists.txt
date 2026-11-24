foreach(state_test usb_audio_state dac_sequence_faults audio_engine_faults)
    echo_add_unit_test(NAME test_${state_test}
        SOURCES test_${state_test}.c
        LIBRARIES echo_device_logic
    )
endforeach()

set(BOARD ${PROJECT_SOURCE_DIR}/platform/device/board)
echo_add_unit_test(NAME test_trial_storage_faults
    SOURCES test_trial_storage_faults.c
        ${BOARD}/trial_boot_module.c
        ${BOARD}/user_state_module.c
    LIBRARIES echo_device_logic echo_settings echo_ui echo_playback
    PRIVATE_INCLUDES ${BOARD} ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/app/include
)
