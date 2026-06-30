# This module defines:
#  LibUring_INCLUDE_DIRS       Location of liburing headers
#  LibUring_LIBRARIES          List of libraries to use liburing
#  LibUring_FOUND              True if liburing was found

find_path(LibUring_INCLUDE_DIR liburing.h)
find_library(LibUring_LIBRARY NAMES uring)

if(LibUring_INCLUDE_DIR AND LibUring_LIBRARY)
    set(LibUring_FOUND TRUE)
endif()

message(STATUS "Looking for LibUring...")

if(NOT LibUring_FOUND)
    if(LibUring_FIND_REQUIRED)
        message(FATAL_ERROR "Cannot find LibUring.")
    elseif(NOT LibUring_FIND_QUIETLY)
        message(WARNING "Cannot find LibUring.")
    endif()
else()
    message(STATUS "...LibUring OK.")
endif()

if(LibUring_FOUND)
    add_library(LibUring STATIC IMPORTED)
    set_target_properties(LibUring PROPERTIES IMPORTED_LOCATION "${LibUring_LIBRARY}")
    set_target_properties(LibUring PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LibUring_INCLUDE_DIR}")
endif()

set(LibUring_INCLUDE_DIRS ${LibUring_INCLUDE_DIR})
set(LibUring_LIBRARIES LibUring)

mark_as_advanced(
        LibUring_INCLUDE_DIR
        LibUring_LIBRARY
)
