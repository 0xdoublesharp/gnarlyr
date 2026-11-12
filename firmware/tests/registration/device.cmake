foreach(device_test audio_math backlight board_pins loader_request update_names update_trial slot_copy updater_echo vop battery_gauge watchdog panic_text stall_watch bt_module card_write core1_control start_record dac_sequence dma_loop flac_accel headset_dips headset_monitor headset_remote jack_sense key_ladder key_scanner msc panel pll sd_card stream_drift tick_clock updater usb_audio usb_audio_hid usb_audio_sink usb_activity usb_leave usb_level usb_device usb_fallback usb_forced usb_plug wav_source pc_profile debug_report power_history rtc_registers time_file wall_clock)
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
echo_add_unit_test(NAME test_board_libc
    SOURCES device/test_board_libc.c ${PROJECT_SOURCE_DIR}/platform/device/board/libc.c
)
target_compile_definitions(test_board_libc PRIVATE
    memcpy=board_memcpy memmove=board_memmove memset=board_memset memcmp=board_memcmp)
target_compile_options(test_board_libc PRIVATE -fno-builtin)

echo_add_unit_test(NAME test_wide_divide
    SOURCES device/test_wide_divide.c
    LIBRARIES echo_device_logic
)
