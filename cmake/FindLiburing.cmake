# Simple FindLiburing.cmake
find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
find_library(LIBURING_LIBRARY NAMES uring)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Liburing DEFAULT_MSG LIBURING_LIBRARY LIBURING_INCLUDE_DIR)

if(LIBURING_FOUND)
    add_library(Liburing::uring UNKNOWN IMPORTED)
    set_target_properties(Liburing::uring PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LIBURING_INCLUDE_DIR}"
        IMPORTED_LOCATION "${LIBURING_LIBRARY}"
    )
endif()
