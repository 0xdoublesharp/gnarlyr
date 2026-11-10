add_library(echo_core STATIC
    dac_identity.c
    deadline.c
    debug_log.c
    format.c
    frame_pacer.c
    frame_stats.c
    hash.c
    key_lock.c
    key_script.c
    key_tracker.c
    low_battery.c
    permutation.c
    power_policy.c
    random.c
    string_builder.c
    utf8.c
)
target_include_directories(echo_core PUBLIC include)
target_link_libraries(echo_core PRIVATE echo_options echo_compact)
