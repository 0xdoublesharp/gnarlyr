add_library(echo_settings STATIC sound_shaping.c parametric_file.c parametric_file_save.c parametric_import.c settings.c equalizer_peak.c equalizer_response.c equalizer_fit.c equalizer_fit_text.c equalizer_slots.c
    rocker.c headphone_profile.c headphone_profile_edit.c headphone_file.c headphone_file_save.c)
target_include_directories(echo_settings PUBLIC include)
target_link_libraries(echo_settings
    PUBLIC echo_core echo_playback
    PRIVATE echo_options echo_compact echo_lto
)
