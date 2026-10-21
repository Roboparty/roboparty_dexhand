cmake_minimum_required(VERSION 3.16)
project(rphand VERSION 0.1.0 LANGUAGES CXX)
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "rphand supports Linux and Windows")
endif()
include(GNUInstallDirs)
option(RPHAND_BUILD_PYTHON "Build Python bindings" OFF)
function(rphand_runtime target install_path)
    if(WIN32)
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "${install_path}")
    # Resolve packaged shared SDK builds relative to their consumers.
    target_link_options(${target} PRIVATE "-Wl,--disable-new-dtags")
endfunction()
function(rphand_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    endif()
endfunction()
find_package(Threads REQUIRED)
if(WIN32)
    set(RPHAND_TRANSPORT src/rphand/backends/gs_usb_winusb.cpp)
else()
    set(RPHAND_TRANSPORT src/rphand/backends/socketcan.cpp)
endif()
add_library(rphand src/hand.cpp src/rphand/transport.cpp ${RPHAND_TRANSPORT}
    src/rphand/protocol.cpp src/rphand/session.cpp)
set_target_properties(rphand PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME rphand
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(rphand::rphand ALIAS rphand)
target_compile_features(rphand PUBLIC cxx_std_17)
target_include_directories(rphand PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rphand PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(rphand PRIVATE winusb setupapi)
    add_executable(rphand_usb_probe examples/cpp/usb_probe.cpp)
    target_include_directories(rphand_usb_probe PRIVATE src)
    target_link_libraries(rphand_usb_probe PRIVATE rphand::rphand)
    install(TARGETS rphand_usb_probe RUNTIME DESTINATION bin)
endif()
rphand_runtime(rphand "$ORIGIN")
rphand_warnings(rphand)
# Keep the original switch working for existing build scripts.
option(RPHAND_BUILD_DEMO "Build C++ examples (wave and state reader)" ON)
if(RPHAND_BUILD_DEMO)
    foreach(example IN ITEMS wave_demo read_state)
        add_executable(rphand_${example} examples/cpp/${example}.cpp)
        target_link_libraries(rphand_${example} PRIVATE rphand::rphand Threads::Threads)
        rphand_warnings(rphand_${example})
        rphand_runtime(rphand_${example} "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        install(TARGETS rphand_${example} RUNTIME DESTINATION bin)
    endforeach()
endif()
install(TARGETS rphand EXPORT rphandTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
include(CMakePackageConfigHelpers)
set(RPHAND_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/rphand")
configure_package_config_file(cmake/rphandConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/rphandConfig.cmake"
    INSTALL_DESTINATION "${RPHAND_CMAKE_INSTALL_DIR}" PATH_VARS CMAKE_INSTALL_LIBDIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/rphandConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT rphandTargets NAMESPACE rphand:: DESTINATION "${RPHAND_CMAKE_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rphandConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/rphandConfigVersion.cmake"
    DESTINATION "${RPHAND_CMAKE_INSTALL_DIR}")
# RPHAND_BUILD_SMOKE remains an alias for older build configurations.
option(RPHAND_BUILD_SMOKE "Legacy alias for RPHAND_BUILD_TESTS" OFF)
option(RPHAND_BUILD_TESTS "Build hardware-free regression tests" OFF)
if(RPHAND_BUILD_TESTS OR RPHAND_BUILD_SMOKE)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RPHAND_BUILD_PYTHON)
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_core python/bindings.cpp)
    target_link_libraries(_core PRIVATE rphand::rphand)
    rphand_runtime(_core "$ORIGIN/lib")
    install(TARGETS _core LIBRARY DESTINATION rphand COMPONENT python
        RUNTIME DESTINATION rphand COMPONENT python)
    install(DIRECTORY python/rphand/
        DESTINATION rphand COMPONENT python
        FILES_MATCHING PATTERN "*.py" PATTERN "*.pyi" PATTERN "py.typed")
    if(BUILD_SHARED_LIBS)
        install(TARGETS rphand
            LIBRARY DESTINATION rphand/lib COMPONENT python
            RUNTIME DESTINATION rphand COMPONENT python)
    endif()
endif()
