set(ECHO_SCREEN_MODULES views)
set(ECHO_SCREEN_MODULE_SOURCES_views
    src/app/modules/views_module.c
    src/app/screens/cover_flow_screen.c
    src/app/screens/up_next_screen.c
    src/ui/cover_flow.c
)

function(echo_screen_module_owner source target_variable name_variable)
    if(NOT source MATCHES "^src/([a-z0-9_]+)/(.+)$")
        message(FATAL_ERROR "module source ${source} must be a file under src/<layer>/")
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
