set(ECHO_SCREEN_MODULES views settings library listening dsp vorbis process)
set(ECHO_SCREEN_MODULE_SOURCES_process
    platform/device/logic/audio_process_entry.c
    platform/device/logic/equalizer_design.c
    platform/device/logic/equalizer.c
)
set(ECHO_SCREEN_MODULE_SOURCES_dsp
    platform/device/logic/audio_interpolate.c
    platform/device/logic/audio_dsp_entry.c
    platform/device/logic/mp4_demux.c
)
set(ECHO_SCREEN_MODULE_SOURCES_vorbis
    platform/device/logic/audio_interpolate.c
    platform/device/logic/audio_vorbis_entry.c
    platform/device/logic/core1_vorbis_source.c
    platform/device/logic/vorbis_stream.c
    platform/device/logic/vorbis_track.c
    src/codec/ogg_packet.c
)
set(ECHO_SCREEN_MODULE_SOURCES_views
    src/app/modules/views_module.c
    src/app/mixtape_store.c
    src/app/mixtape_ui.c
    src/app/mixtape_screen.c
    src/playback/mix_catalog.c
    src/playback/mix_resolve.c
    src/playback/mix_tape.c
    src/app/screens/cover_flow_screen.c
    src/app/screens/up_next_screen.c
    src/ui/cover_flow.c
)
set(ECHO_SCREEN_MODULE_SOURCES_settings
    src/app/language_list.c
    src/app/modules/settings_module.c
    src/app/screens/charge_art.c
    src/app/settings_screens.c
    src/app/button_settings.c
    src/app/equalizer_presets.c
    src/app/diagnostics_rows.c
    src/app/diagnostics_values.c
    src/app/screens/bluetooth_screen.c
    src/app/screens/equalizer_screen.c
    src/app/screens/equalizer_browser.c
    src/app/screens/equalizer_plot.c
    src/settings/equalizer_response.c
    src/core/dac_identity.c
    src/settings/equalizer_slots.c
    src/ui/choice_screen.c
)
set(ECHO_SCREEN_MODULE_SOURCES_library
    src/app/modules/library_module.c
    src/app/music_walk.c
    src/app/discovery_screen.c
    src/app/discovery_text.c
    src/app/discovery_preferences.c
    src/app/discovery_playback.c
    src/app/personal_tags.c
    src/library/music_fingerprint.c
    src/library/library_view.c
    src/library/sort_key.c
    src/library/sort_fold_table.c
)
set(ECHO_SCREEN_MODULE_SOURCES_listening
    src/app/modules/listening_module.c
    src/app/screens/listening_screen.c
    src/app/screens/lyrics_screen.c
    src/playback/listening_stats.c
    src/playback/listening_export.c
    src/library/lyrics.c
)
if(ECHO_RELEASE)
    list(APPEND ECHO_SCREEN_MODULE_SOURCES_settings src/app/diagnostics_menu_release.c)
else()
    list(APPEND ECHO_SCREEN_MODULE_SOURCES_settings src/app/diagnostics_menu.c)
endif()

function(echo_screen_module_owner source target_variable name_variable)
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
