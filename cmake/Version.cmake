# Resolve the version from the nearest reachable vMAJOR.MINOR.PATCH tag.
function(logalizer_resolve_version source_dir)
    set(version "0.0.0")
    set(display "0.0.0-dev")
    set(hash "unknown")
    set(build "0")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${source_dir}" RESULT_VARIABLE git_result
            OUTPUT_VARIABLE git_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(git_result EQUAL 0)
            set(hash "${git_hash}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
                WORKING_DIRECTORY "${source_dir}" OUTPUT_VARIABLE build
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --long --match "v[0-9]*" HEAD
                WORKING_DIRECTORY "${source_dir}" OUTPUT_VARIABLE description
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(description MATCHES "^v(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)-([0-9]+)-g[0-9a-f]+$")
                set(version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
                set(distance "${CMAKE_MATCH_4}")
                if(distance EQUAL 0)
                    set(display "${version}")
                else()
                    set(display "${version}-dev.${distance}+${hash}")
                endif()
            else()
                set(display "${version}-dev+${hash}")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
                WORKING_DIRECTORY "${source_dir}" OUTPUT_VARIABLE dirty
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT dirty STREQUAL "")
                string(APPEND display " (dirty)")
            endif()
        endif()
    endif()
    if(NOT build MATCHES "^[0-9]+$")
        set(build "0")
    endif()
    set(LOGALIZER_VERSION "${version}" PARENT_SCOPE)
    set(LOGALIZER_VERSION_DISPLAY "${display}" PARENT_SCOPE)
    set(GIT_COMMIT_HASH "${hash}" PARENT_SCOPE)
    set(GIT_BUILD_NUMBER "${build}" PARENT_SCOPE)
endfunction()
