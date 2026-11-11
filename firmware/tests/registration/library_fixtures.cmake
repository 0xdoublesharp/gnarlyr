add_library(echo_library_fixtures STATIC
    library/memory_source.c
    library/fixture_library.c
    library/big_library.c
    library/spoken_library.c
)
target_include_directories(echo_library_fixtures PUBLIC library)
target_compile_definitions(echo_library_fixtures PRIVATE
    ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(echo_library_fixtures PUBLIC echo_library echo_library_scan PRIVATE echo_options)
