add_library(echo_fonts STATIC fonts.c)
target_include_directories(echo_fonts PUBLIC include)
target_link_libraries(echo_fonts
    PUBLIC echo_gfx
    PRIVATE echo_options
)

if(CMAKE_CROSSCOMPILING AND ECHO_BOARD STREQUAL "ECHO")
    target_compile_definitions(echo_fonts PRIVATE ECHO_FONTS_PACKED)
endif()

file(READ ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json ECHO_GLYPH_PACK_CONFIG)
string(JSON ECHO_GLYPH_FACE_COUNT LENGTH "${ECHO_GLYPH_PACK_CONFIG}" faces)
math(EXPR ECHO_GLYPH_LAST_FACE "${ECHO_GLYPH_FACE_COUNT} - 1")
set(ECHO_GLYPH_FACE_FILES "")
foreach(face RANGE ${ECHO_GLYPH_LAST_FACE})
    string(JSON face_name GET "${ECHO_GLYPH_PACK_CONFIG}" faces ${face} name)
    string(JSON face_board ERROR_VARIABLE face_board_error GET "${ECHO_GLYPH_PACK_CONFIG}" faces ${face} board)
    if(face_board_error OR face_board STREQUAL ECHO_BOARD)
        list(APPEND ECHO_GLYPH_FACE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/packs/${face_name}.bin)
    endif()
endforeach()
set(ECHO_GLYPH_FACE_FILES "${ECHO_GLYPH_FACE_FILES}" CACHE INTERNAL "glyph pack faces in face order")
set_property(DIRECTORY ${PROJECT_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json
)
