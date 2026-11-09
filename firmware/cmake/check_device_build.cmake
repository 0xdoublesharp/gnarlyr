foreach(step "--preset;${PRESET}" "--build;--preset;${PRESET}")
    execute_process(COMMAND ${CMAKE_COMMAND} ${step}
        WORKING_DIRECTORY ${SOURCE_DIR}
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "cmake ${step} failed")
    endif()
endforeach()
