echo_add_unit_test(NAME test_deadline SOURCES core/test_deadline.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_headphone_audio SOURCES core/test_headphone_audio.c LIBRARIES echo_core)
if(UNIX)
    target_link_libraries(test_headphone_audio PRIVATE m)
endif()
echo_add_unit_test(NAME test_frame_pacer SOURCES core/test_frame_pacer.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_frame_stats SOURCES core/test_frame_stats.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_key_tracker SOURCES core/test_key_tracker.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_key_lock SOURCES core/test_key_lock.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_utf8 SOURCES core/test_utf8.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_string_builder SOURCES core/test_string_builder.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_format SOURCES core/test_format.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_calendar SOURCES core/test_calendar.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_clock_face SOURCES core/test_clock_face.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_dac_identity SOURCES core/test_dac_identity.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_random SOURCES core/test_random.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_permutation SOURCES core/test_permutation.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_hash SOURCES core/test_hash.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_i18n SOURCES core/test_i18n.c LIBRARIES echo_core echo_test_pack)
echo_add_unit_test(NAME test_i18n_fallback
    SOURCES core/test_i18n_fallback.c support/test_pack.c ${PROJECT_SOURCE_DIR}/src/core/i18n.c
    LIBRARIES echo_core
)
echo_add_unit_test(NAME test_language_packs
    SOURCES core/test_language_packs.c
    LIBRARIES echo_core echo_test_pack
)
target_compile_definitions(test_language_packs PRIVATE
    ECHO_ENGLISH_PACK="${ECHO_ENGLISH_PACK}"
    ECHO_PSEUDO_PACK="${ECHO_PSEUDO_PACK}"
)
add_dependencies(test_language_packs echo_test_language_packs)
echo_add_unit_test(NAME test_power_policy SOURCES core/test_power_policy.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_low_battery SOURCES core/test_low_battery.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_key_script SOURCES core/test_key_script.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_debug_log SOURCES core/test_debug_log.c LIBRARIES echo_core)
echo_add_unit_test(NAME test_debug_script SOURCES core/test_debug_script.c LIBRARIES echo_core)
