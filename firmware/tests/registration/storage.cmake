echo_add_unit_test(NAME test_fat
    SOURCES device/test_fat.c device/sparse_card.c
    LIBRARIES echo_device_logic
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/platform/device/logic
)
target_compile_definitions(test_fat PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_fat PROPERTIES TIMEOUT 20)
echo_add_unit_test(NAME test_fat_extents
    SOURCES device/test_fat_extents.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat_extents PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_fat_extents PROPERTIES TIMEOUT 20)
echo_add_unit_test(NAME test_stored_queue
    SOURCES device/test_stored_queue.c device/sparse_card.c
    LIBRARIES echo_device_logic echo_playback echo_library_fixtures
)
target_compile_definitions(test_stored_queue PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_stored_queue PROPERTIES TIMEOUT 30)
echo_add_unit_test(NAME test_workspace_loan
    SOURCES device/test_workspace_loan.c
    LIBRARIES echo_device_logic
)
echo_add_unit_test(NAME test_card_format
    SOURCES device/test_card_format.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_card_format PRIVATE
    ECHO_FORMAT_IMAGE="${CMAKE_CURRENT_BINARY_DIR}/card-format-test.img"
)
set_tests_properties(test_card_format PROPERTIES RESOURCE_LOCK card_format_images TIMEOUT 120)

set(ECHO_CREATED_CARDS ${CMAKE_CURRENT_BINARY_DIR}/created_cards)
file(MAKE_DIRECTORY ${ECHO_CREATED_CARDS})
echo_add_unit_test(NAME test_fat_create
    SOURCES device/test_fat_create.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat_create PRIVATE
    ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ECHO_CREATED_DIR="${ECHO_CREATED_CARDS}"
)
set_tests_properties(test_fat_create PROPERTIES TIMEOUT 30 FIXTURES_SETUP created_cards)
echo_add_unit_test(NAME test_block_read SOURCES device/test_block_read.c LIBRARIES echo_device_logic)
echo_add_unit_test(NAME test_fat_delete
    SOURCES device/test_fat_delete.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_fat_delete PRIVATE
    ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ECHO_CREATED_DIR="${ECHO_CREATED_CARDS}"
)
set_tests_properties(test_fat_delete PROPERTIES TIMEOUT 30 FIXTURES_SETUP created_cards)
echo_add_unit_test(NAME test_volume_space
    SOURCES device/test_volume_space.c device/sparse_card.c
    LIBRARIES echo_device_logic
)
target_compile_definitions(test_volume_space PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
