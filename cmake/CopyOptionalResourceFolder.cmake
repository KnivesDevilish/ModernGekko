# Copies one optional folder next to an executable, at build time, such as
# into its Resources/: cmake -DSOURCE=<folder> -DDESTINATION=<folder> -P this.
# The copy follows the folder: a changed file is copied again and a file no
# longer in it is removed from the copy. A missing folder only warns, since
# what uses it works without it.
cmake_minimum_required(VERSION 3.20)

if(NOT IS_DIRECTORY "${SOURCE}")
    message(WARNING "Optional folder not found, left out: ${SOURCE}")
    return()
endif()

file(GLOB_RECURSE _files LIST_DIRECTORIES false RELATIVE "${SOURCE}" "${SOURCE}/*")
if(IS_DIRECTORY "${DESTINATION}")
    file(GLOB_RECURSE _copied LIST_DIRECTORIES false RELATIVE "${DESTINATION}"
         "${DESTINATION}/*")
    foreach(_file IN LISTS _copied)
        if(NOT _file IN_LIST _files)
            file(REMOVE "${DESTINATION}/${_file}")
        endif()
    endforeach()
endif()

foreach(_file IN LISTS _files)
    get_filename_component(_directory "${DESTINATION}/${_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_directory}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}/${_file}"
                "${DESTINATION}/${_file}"
        RESULT_VARIABLE _result)
    if(_result)
        message(FATAL_ERROR "Could not copy ${SOURCE}/${_file} to ${DESTINATION}")
    endif()
endforeach()
