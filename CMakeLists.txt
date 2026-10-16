cmake_minimum_required(VERSION 3.16)
project(rphand VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "rphand requires Linux SocketCAN")
endif()
include(GNUInstallDirs)
option(RPHAND_BUILD_PYTHON "Build Python bindings" OFF)
function(rphand_runtime target install_path)
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "${install_path}")
    # Resolve packaged shared SDK builds relative to their consumers.
    target_link_options(${target} PRIVATE "-Wl,--disable-new-dtags")
endfunction()
find_package(Threads REQUIRED)
add_library(rphand src/hand.cpp src/rphand/socketcan.cpp
    src/rphand/protocol.cpp src/rphand/session.cpp)
set_target_properties(rphand PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME rphand)
add_library(rphand::rphand ALIAS rphand)
target_compile_features(rphand PUBLIC cxx_std_17)
target_include_directories(rphand PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rphand PRIVATE Threads::Threads)
rphand_runtime(rphand "$ORIGIN")
target_compile_options(rphand PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
option(RPHAND_BUILD_DEMO "Build dual-hand wave demo" ON)
if(RPHAND_BUILD_DEMO)
    add_executable(rphand_wave_demo examples/wave_demo.cpp)
    target_link_libraries(rphand_wave_demo PRIVATE rphand::rphand Threads::Threads)
    target_compile_options(rphand_wave_demo PRIVATE -Wall -Wextra -Wpedantic)
    rphand_runtime(rphand_wave_demo "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS rphand_wave_demo RUNTIME DESTINATION bin)
endif()
install(TARGETS rphand EXPORT rphandTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
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
option(RPHAND_BUILD_SMOKE "Build a small hardware-free API/lifecycle check" OFF)
if(RPHAND_BUILD_SMOKE)
    enable_testing()
    foreach(test IN ITEMS basic_smoke canfd_backend session_state)
        add_executable(rphand_${test} tests/${test}.cpp)
        target_include_directories(rphand_${test} PRIVATE src)
        target_link_libraries(rphand_${test} PRIVATE rphand::rphand Threads::Threads)
        add_test(NAME rphand_${test} COMMAND rphand_${test})
        set_tests_properties(rphand_${test} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(rphand_hand_cleanup tests/hand_cleanup.cpp src/hand.cpp
        src/rphand/protocol.cpp src/rphand/session.cpp)
    target_include_directories(rphand_hand_cleanup PRIVATE include src)
    target_compile_features(rphand_hand_cleanup PRIVATE cxx_std_17)
    target_compile_options(rphand_hand_cleanup PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(rphand_hand_cleanup PRIVATE Threads::Threads)
    add_test(NAME rphand_hand_cleanup COMMAND rphand_hand_cleanup)
    set_tests_properties(rphand_hand_cleanup PROPERTIES TIMEOUT 20)
    if(RPHAND_BUILD_DEMO)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME rphand_examples COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/examples.py)
            set_tests_properties(rphand_examples PROPERTIES TIMEOUT 20)
        endif()
    endif()
endif()

if(RPHAND_BUILD_PYTHON)
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_core python/bindings.cpp)
    target_link_libraries(_core PRIVATE rphand::rphand)
    rphand_runtime(_core "$ORIGIN/lib")
    install(TARGETS _core LIBRARY DESTINATION rphand COMPONENT python)
    install(DIRECTORY python/rphand/
        DESTINATION rphand COMPONENT python
        FILES_MATCHING PATTERN "*.py" PATTERN "*.pyi" PATTERN "py.typed")
    if(BUILD_SHARED_LIBS)
        install(TARGETS rphand
            LIBRARY DESTINATION rphand/lib COMPONENT python)
    endif()
endif()
