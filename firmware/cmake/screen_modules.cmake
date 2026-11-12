set(ECHO_SCREEN_MODULES views settings library listening dsp ogg process extension playing effects source dsd equalizer trial)
set(ECHO_SCREEN_MODULE_SHARED_SOURCES src/codec/flac.c src/codec/flac_bits.c)
set(ECHO_SCREEN_MODULE_SOURCES_process
    src/core/parametric.c
    src/core/parametric_curve.c
    src/core/parametric_bound.c
    platform/device/logic/audio_engine_stage.c
    platform/device/logic/audio_shape.c
    platform/device/logic/audio_process_entry.c
    platform/device/logic/audio_trig.c
    platform/device/logic/audio_tempo.c
    platform/device/logic/audio_tempo_stage.c
    platform/device/logic/equalizer_design.c
    platform/device/logic/equalizer.c
)
set(ECHO_SCREEN_MODULE_SOURCES_effects
    platform/device/logic/audio_effects_entry.c
    platform/device/logic/audio_fade.c
)
set(ECHO_SCREEN_MODULE_SOURCES_source
    platform/device/logic/audio_source_entry.c
    platform/device/logic/core1_open_flac.c
    platform/device/logic/core1_open_mp3.c
    platform/device/logic/mp3_probe.c
    platform/device/logic/flac_source_open.c
    src/codec/flac.c
    src/codec/flac_bits.c
)
set(ECHO_SCREEN_MODULE_SOURCES_dsp
    platform/device/logic/aiff_format.c
    platform/device/logic/audio_interpolate.c
    platform/device/logic/audio_dsp_entry.c
    platform/device/logic/mp4_demux.c
)
set(ECHO_SCREEN_MODULE_SOURCES_dsd
    platform/device/logic/audio_dsd_entry.c
    platform/device/logic/audio_interpolate.c
    platform/device/logic/core1_dsd_source.c
    platform/device/logic/dsd_stream.c
    platform/device/logic/dsd_track.c
)
set(ECHO_SCREEN_MODULE_SOURCES_ogg
    platform/device/logic/audio_interpolate.c
    platform/device/logic/audio_ogg_entry.c
    platform/device/logic/core1_ogg_source.c
    platform/device/logic/core1_opus_source.c
    platform/device/logic/core1_vorbis_source.c
    platform/device/logic/ogg_stream.c
    platform/device/logic/ogg_track.c
    platform/device/logic/opus_track.c
    platform/device/logic/vorbis_track.c
    src/codec/ogg_packet.c
)
set(ECHO_SCREEN_MODULE_SOURCES_trial
    platform/device/board/trial_boot_module.c
    platform/device/board/trial_record.c
    platform/device/logic/fat_delete.c
    platform/device/logic/slot_copy.c
    platform/device/logic/update_trial.c
)
set(ECHO_SCREEN_MODULE_SOURCES_extension
    src/app/standby.c
    src/app/modules/extension_module.c
    src/app/favorite_playlist_add.c
    src/app/menu_hold.c
    src/app/headphone_store.c
    src/app/headphone_parameters.c
    src/settings/headphone_file.c
    src/settings/headphone_profile.c
    platform/device/board/file_remove.c
    platform/device/board/trial_module.c
    platform/device/board/trial_record.c
    platform/device/logic/slot_copy.c
    platform/device/logic/fat_delete.c
    platform/device/logic/updater_install.c
    platform/device/board/debug_rtc.c
    platform/device/logic/rtc_time.c
    platform/device/logic/rtc_registers.c
    src/core/calendar.c
    src/gfx/wireframe.c
    src/ui/wire_figure.c
    src/ui/wire_motion.c
)
set(ECHO_SCREEN_MODULE_SOURCES_playing
    src/app/modules/playing_module.c
    src/app/screens/cover_flow_screen.c
    src/app/board_cover_layout.c
    src/app/cover_cache_half.c
    src/core/random_below.c
    src/app/cover_placeholders.c
    src/app/screens/up_next_screen.c
    src/app/queue_actions.c
    src/app/queue_edits.c
    src/ui/cover_flow.c
    src/ui/placeholder_art.c
    src/gfx/image_quad.c
    src/app/screens/quick_settings_screen.c
    src/app/settings_actions.c
    src/app/settings_actions_equalizer.c
    src/app/equalizer_presets.c
    src/settings/equalizer_slots.c
    src/settings/equalizer_fit_text.c
    src/app/headphone_store_edit.c
    src/app/headphone_parameters.c
    src/settings/headphone_profile.c
    src/settings/headphone_profile_edit.c
    src/settings/headphone_file_save.c
    src/core/headphone_audio_switch.c
    src/library/sort_initial.c
    src/library/sort_key.c
    src/library/sort_fold_table.c
    src/app/parametric_store.c
    src/app/parametric_store_save.c
    src/settings/parametric_import.c
    src/settings/parametric_file.c
    src/settings/parametric_file_save.c
    src/core/parametric.c
    src/settings/sound_shaping.c
    src/app/browse_sections.c
)
set(ECHO_SCREEN_MODULE_SOURCES_equalizer
    src/settings/sound_shaping.c
    src/app/parametric_store.c
    src/settings/parametric_file.c
    src/ui/choice_screen.c
    src/app/equalizer_ui.c
    src/core/parametric.c
    src/core/parametric_curve.c
    src/core/parametric_bound.c
    src/app/parametric_store_save.c
    src/settings/parametric_file_save.c
    src/settings/parametric_import.c
    src/app/equalizer_presets.c
    src/app/screens/equalizer_screen.c
    src/app/screens/parametric_screen.c
    src/app/screens/sound_shaping_screen.c
    src/app/screens/equalizer_browser.c
    src/app/screens/equalizer_plot.c
    src/settings/equalizer_response.c
    src/settings/equalizer_slots.c
    src/settings/equalizer_fit_text.c
    src/app/modules/equalizer_module.c
)
set(ECHO_SCREEN_MODULE_SOURCES_views
    src/app/theme_pack.c
    src/app/theme_runtime.c
    src/app/theme_catalog.c
    src/app/theme_picker.c
    src/app/theme_palette.c
    src/app/theme_signature.c
    src/app/theme_crypto_curve.c
    src/app/theme_crypto_sha512.c
    src/app/modules/views_module.c
    platform/device/board/headset_reading.c
    platform/device/logic/headset_dips_view.c
    platform/device/logic/headset_monitor_reading.c
    src/app/screens/charge_rate.c
    src/app/screens/charge_art.c
    src/app/screens/lyrics_screen.c
    src/library/lyrics.c
    src/app/diagnostics_rows.c
    src/app/diagnostics_values.c
    src/app/mixtape_store.c
    src/app/order_adopt.c
    src/app/mixtape_ui.c
    src/app/cassette_ui.c
    src/playback/cassette_budget.c
    src/app/mixtape_screen.c
    src/playback/mix_catalog.c
    src/playback/mix_resolve.c
    src/playback/mix_tape.c
    src/app/screens/calendar_screen.c
    src/core/calendar.c
    src/core/calendar_view.c
    src/app/screens/cassette_flip_screen.c
    src/app/screens/clock_screen.c
    src/app/queue_actions.c
    src/app/queue_edits.c
    src/library/sort_key.c
    src/library/sort_initial.c
    src/library/sort_fold_table.c
)
set(ECHO_SCREEN_MODULE_UI_SOURCES src/app/home_art.c src/app/home_art_backdrop.c
    src/app/theme_art.c)
list(APPEND ECHO_SCREEN_MODULE_SOURCES_playing ${ECHO_SCREEN_MODULE_UI_SOURCES})
set(ECHO_SCREEN_MODULE_FAST_SOURCES src/gfx/image_quad.c)
set(ECHO_SCREEN_MODULE_SOURCES_settings
    platform/device/board/card_eject.c
    platform/device/board/sd_allocation.c
    src/app/app_screens_home.c
    platform/device/board/card_format.c
    platform/device/logic/card_format.c
    src/app/screens/card_format_screen.c
    src/app/headphone_editor.c
    src/app/headphone_store_edit.c
    src/app/headphone_parameters.c
    src/settings/headphone_profile.c
    src/app/audio_settings.c
    src/settings/headphone_profile_edit.c
    src/settings/headphone_file_save.c
    src/core/headphone_audio_switch.c
    src/app/language_list.c
    src/app/modules/settings_module.c
    src/app/settings_screens.c
    src/app/plug_modes.c
    src/app/screens/plug_chooser.c
    src/app/screens/plug_art.c
    src/gfx/wireframe.c
    src/app/screens/set_clock_screen.c
    src/core/calendar.c
    src/app/settings_actions.c
    src/app/settings_preview.c
    src/app/button_settings.c
    src/app/home_editor.c
    src/app/screens/bluetooth_screen.c
    src/app/bluetooth_link_ui.c
    src/core/dac_identity.c
    src/ui/choice_screen.c
)
set(ECHO_SCREEN_MODULE_SOURCES_library
    src/app/modules/library_module.c
    src/library/album_order.c
    src/library/sort_initial.c
    src/app/music_walk.c
    src/app/discovery_screen.c
    src/app/discovery_text.c
    src/app/discovery_preferences.c
    src/app/discovery_playback.c
    src/app/order_publish.c
    src/app/order_adopt.c
    src/app/personal_tags.c
    src/library/music_fingerprint.c
    src/library/library_view.c
    src/library/sort_key.c
    src/library/sort_fold_table.c
)
set(ECHO_SCREEN_MODULE_SOURCES_listening
    src/app/modules/listening_module.c
    src/core/calendar.c
    src/app/listening_store_view.c
    src/app/screens/listening_screen.c
    src/app/screens/moments_screen.c
    src/playback/moments.c
    src/app/screens/listening_work.c
    src/app/screens/recap_cards.c
    src/app/screens/smart_list_screen.c
    src/app/order_publish.c
    src/app/order_adopt.c
    src/playback/listening_cursor.c
    src/playback/listening_stats.c
    src/playback/listening_recap.c
    src/playback/smart_list.c
    src/playback/track_keys.c
    src/playback/listening_export.c
    src/playback/listening_clear.c
    src/app/listening_clear.c
    src/app/cover_cache_half.c
)
if(ECHO_RELEASE)
    list(APPEND ECHO_SCREEN_MODULE_SOURCES_views src/app/diagnostics_menu_release.c)
else()
    list(APPEND ECHO_SCREEN_MODULE_SOURCES_views src/app/diagnostics_menu.c)
    list(APPEND ECHO_SCREEN_MODULE_SOURCES_effects
        src/app/modules/debug_module.c
        src/app/debug_steps.c
        src/app/debug_screenshot.c
        platform/device/board/debug_poke.c
        platform/device/board/debug_rtc.c
        platform/device/logic/rtc_time.c
        platform/device/logic/rtc_registers.c
        src/core/calendar.c
    )
endif()

function(echo_screen_module_owner source target_variable name_variable)
    if(source MATCHES "^platform/device/(board/.+)$")
        set(${target_variable} echo_device PARENT_SCOPE)
        set(${name_variable} ${CMAKE_MATCH_1} PARENT_SCOPE)
        return()
    endif()
    if(source MATCHES "^platform/device/logic/(.+)$")
        set(${target_variable} echo_device_logic PARENT_SCOPE)
        set(${name_variable} ${CMAKE_MATCH_1} PARENT_SCOPE)
        return()
    endif()
    if(NOT source MATCHES "^src/([a-z0-9_]+)/(.+)$")
        message(FATAL_ERROR "module source ${source} must belong to a known firmware target")
    endif()
    set(${target_variable} echo_${CMAKE_MATCH_1} PARENT_SCOPE)
    set(${name_variable} ${CMAKE_MATCH_2} PARENT_SCOPE)
endfunction()

function(echo_take_module_sources)
    set(taken "")
    foreach(module IN LISTS ECHO_SCREEN_MODULES)
        foreach(source IN LISTS ECHO_SCREEN_MODULE_SOURCES_${module})
            echo_screen_module_owner(${source} owner name)
            if(owner STREQUAL "echo_device" OR source IN_LIST ECHO_SCREEN_MODULE_SHARED_SOURCES)
                continue()
            endif()
            get_target_property(sources ${owner} SOURCES)
            if(name IN_LIST sources)
                list(REMOVE_ITEM sources ${name})
                set_property(TARGET ${owner} PROPERTY SOURCES ${sources})
                list(APPEND taken ${source})
            elseif(NOT source IN_LIST taken)
                message(FATAL_ERROR "module ${module}: ${source} is not a source of ${owner}")
            endif()
        endforeach()
    endforeach()
endfunction()
