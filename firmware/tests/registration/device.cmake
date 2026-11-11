foreach(device_test audio_math backlight board_pins loader_request update_names updater_echo vop battery_gauge watchdog panic_text stall_watch bt_module card_write core1_control start_record dac_sequence dma_loop flac_accel headset_dips headset_monitor headset_remote jack_sense key_ladder key_scanner msc panel pll sd_card stream_drift tick_clock updater usb_audio usb_audio_sink usb_device usb_fallback usb_forced usb_plug wav_source pc_profile debug_report power_history)
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
