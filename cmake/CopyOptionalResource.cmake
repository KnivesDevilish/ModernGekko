# Copies one optional image into an executable's Resources/ folder, at build
# time: cmake -DSOURCE=<image> -DDESTINATION=<folder>/Resources/<name> -P this.
# An image that changed is copied again; one that is missing only warns, since
# every window that uses one shows its text without it.
if(NOT EXISTS "${SOURCE}")
    message(WARNING "Optional image not found, left out: ${SOURCE}")
    return()
endif()
get_filename_component(_destination_dir "${DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${_destination_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}" "${DESTINATION}"
    RESULT_VARIABLE _result)
if(_result)
    message(FATAL_ERROR "Could not copy ${SOURCE} to ${DESTINATION}")
endif()
