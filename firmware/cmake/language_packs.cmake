find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ECHO_LANGUAGE_TOOL ${PROJECT_SOURCE_DIR}/tools/lang_pack.py)
set(ECHO_LANGUAGE_SOURCES ${PROJECT_SOURCE_DIR}/assets/lang)
set(ECHO_TEST_LANGUAGE qps)

function(echo_add_language_pack code output_variable)
    set(pack ${CMAKE_BINARY_DIR}/lang/${code}.pack)
    set(sources ${ECHO_LANGUAGE_SOURCES}/en.txt)
    if(EXISTS ${ECHO_LANGUAGE_SOURCES}/${code}.txt)
        list(APPEND sources ${ECHO_LANGUAGE_SOURCES}/${code}.txt)
    endif()
    add_custom_command(
        OUTPUT ${pack}
        COMMAND ${Python3_EXECUTABLE} ${ECHO_LANGUAGE_TOOL}
            --languages ${ECHO_LANGUAGE_SOURCES} --pack ${code} --output ${pack}
        DEPENDS ${sources} ${ECHO_LANGUAGE_TOOL}
            ${PROJECT_SOURCE_DIR}/assets/fonts/fonts.json
            ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json
        VERBATIM
    )
    set(${output_variable} ${pack} PARENT_SCOPE)
endfunction()

if(NOT ECHO_LANGUAGES)
    message(FATAL_ERROR "ECHO_LANGUAGES is empty: an image needs at least one language pack")
endif()
if(ECHO_TEST_LANGUAGE IN_LIST ECHO_LANGUAGES)
    message(FATAL_ERROR "ECHO_LANGUAGES: ${ECHO_TEST_LANGUAGE} is the test language and is never built into an image")
endif()

set(ECHO_LANGUAGE_PACK_FILES "")
foreach(code IN LISTS ECHO_LANGUAGES)
    echo_add_language_pack(${code} pack)
    list(APPEND ECHO_LANGUAGE_PACK_FILES ${pack})
endforeach()
add_custom_target(echo_language_packs DEPENDS ${ECHO_LANGUAGE_PACK_FILES})
