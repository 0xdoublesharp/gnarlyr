echo_add_unit_test(NAME test_equalizer
    SOURCES device/test_equalizer.c
    LIBRARIES echo_device_logic echo_settings
)
echo_add_unit_test(NAME test_audio_resample
    SOURCES device/test_audio_resample.c
    LIBRARIES echo_device_logic
)
if(UNIX)
    target_link_libraries(test_audio_resample PRIVATE m)
endif()
add_executable(audio_tempo_tool device/audio_tempo_tool.c)
target_link_libraries(audio_tempo_tool PRIVATE echo_device_logic echo_options)
echo_add_unit_test(NAME test_audio_tempo
    SOURCES device/test_audio_tempo.c
    LIBRARIES echo_device_logic
)
add_dependencies(test_audio_tempo audio_tempo_tool)
echo_add_unit_test(NAME test_audio_tempo_stage
    SOURCES device/test_audio_tempo_stage.c
    LIBRARIES echo_device_logic
)
if(UNIX)
    target_link_libraries(test_audio_tempo PRIVATE m)
    target_link_libraries(test_audio_tempo_stage PRIVATE m)
endif()
echo_add_unit_test(NAME test_audio_engine
    SOURCES device/test_audio_engine.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_audio_engine PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_audio_fade
    SOURCES device/test_audio_fade.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_audio_engine_pcm
    SOURCES device/test_audio_engine_pcm.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_audio_engine_tempo
    SOURCES device/test_audio_engine_tempo.c
    LIBRARIES echo_device_logic
)
if(UNIX)
    target_link_libraries(test_audio_engine_tempo PRIVATE m)
endif()
echo_add_unit_test(NAME test_mp3_probe
    SOURCES device/test_mp3_probe.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_mp3_probe PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_mp3_decoder
    SOURCES device/test_mp3_decoder.c
    LIBRARIES echo_device_logic echo_libmad
)
target_compile_definitions(test_mp3_decoder PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_mp4_demux
    SOURCES device/test_mp4_demux.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_mp4_demux PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_mp4_demux PRIVATE device)
add_executable(mp3_decode_tool device/mp3_decode_tool.c device/m4a_fixture_files.c)
target_link_libraries(mp3_decode_tool PRIVATE echo_device_logic echo_options)
target_include_directories(mp3_decode_tool PRIVATE device)
target_compile_definitions(mp3_decode_tool PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_executable(m4a_stream_tool device/m4a_stream_tool.c device/m4a_fixture_files.c)
target_link_libraries(m4a_stream_tool PRIVATE echo_device_logic echo_options)
target_include_directories(m4a_stream_tool PRIVATE device)
target_compile_definitions(m4a_stream_tool PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_m4a_source
    SOURCES device/test_m4a_source.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_m4a_source PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_m4a_source PRIVATE device)
echo_add_unit_test(NAME test_vorbis_source
    SOURCES device/test_vorbis_source.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_vorbis_source PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_vorbis_seek
    SOURCES device/test_vorbis_seek.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_vorbis_seek PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_vorbis_stream
    SOURCES device/test_vorbis_stream.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_vorbis_stream PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_executable(vorbis_corpus_tool device/vorbis_corpus_tool.c)
target_link_libraries(vorbis_corpus_tool PRIVATE echo_device_logic echo_options)

echo_add_unit_test(NAME test_vorbis_memory
    SOURCES device/test_vorbis_memory.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_opus_track
    SOURCES device/test_opus_track.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_opus_track PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_opus_source
    SOURCES device/test_opus_source.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_opus_source PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_opus_decode
    SOURCES device/test_opus_decode.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_vorbis_decoder
    SOURCES device/test_vorbis_decoder.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_vorbis_decoder PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_core1_stream
    SOURCES device/test_core1_stream.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_core1_stream PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

echo_add_unit_test(NAME test_dsd_source
    SOURCES device/test_dsd_source.c
    LIBRARIES echo_device_logic
)

add_executable(dsd_stream_tool device/dsd_stream_tool.c)
target_link_libraries(dsd_stream_tool PRIVATE echo_device_logic echo_options)

echo_add_unit_test(NAME test_audio_effects
    SOURCES device/test_audio_effects.c
    LIBRARIES echo_device_logic
)
if(UNIX)
    target_link_libraries(test_audio_effects PRIVATE m)
endif()
echo_add_unit_test(NAME test_ape_track
    SOURCES device/test_ape_track.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_ape_decode
    SOURCES device/test_ape_decode.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_ape_decode PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_ape_decode PRIVATE device)
add_executable(ape_stream_tool device/ape_stream_tool.c)
target_link_libraries(ape_stream_tool PRIVATE echo_device_logic echo_options)
echo_add_unit_test(NAME test_ape_source
    SOURCES device/test_ape_source.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_ape_source PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_ape_source PRIVATE device)

echo_add_unit_test(NAME test_asf_track
    SOURCES device/test_asf_track.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_asf_track PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_asf_track PRIVATE device)
add_executable(wma_decode_tool device/wma_decode_tool.c)
target_link_libraries(wma_decode_tool PRIVATE echo_device_logic echo_options)

add_test(NAME wma_reference
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/emulator/test_core1_wma.py
        --tool $<TARGET_FILE:wma_decode_tool>
        --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/wma
)
echo_add_unit_test(NAME test_wma_source
    SOURCES device/test_wma_source.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_wma_source PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_wma_source PRIVATE device)
echo_add_unit_test(NAME test_wma_decode
    SOURCES device/test_wma_decode.c device/m4a_fixture_files.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_wma_decode PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(test_wma_decode PRIVATE device)

add_test(NAME wma_windows_up_to_date
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/wma_tables.py
        --output ${PROJECT_SOURCE_DIR}/third_party/rockbox_wma/port/wma_windows.h
        --style ${PROJECT_SOURCE_DIR}/.clang-format --check
)
