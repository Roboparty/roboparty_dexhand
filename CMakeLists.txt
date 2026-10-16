cmake_minimum_required(VERSION 3.16)
project(roboparty_dexhand_sdk VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "roboparty_dexhand_sdk requires Linux SocketCAN")
endif()
include(GNUInstallDirs)
option(ROBOPARTY_BUILD_PYTHON "Build Python bindings" OFF)
function(rphand_runtime target install_path)
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "${install_path}")
    # Resolve packaged shared SDK builds relative to their consumers.
    target_link_options(${target} PRIVATE "-Wl,--disable-new-dtags")
endfunction()
find_package(Threads REQUIRED)
add_library(roboparty_dexhand_sdk src/hand.cpp src/rphand/socketcan.cpp
    src/rphand/protocol.cpp src/rphand/session.cpp)
set_target_properties(roboparty_dexhand_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME dexhand_sdk)
add_library(roboparty::dexhand_sdk ALIAS roboparty_dexhand_sdk)
target_compile_features(roboparty_dexhand_sdk PUBLIC cxx_std_17)
target_include_directories(roboparty_dexhand_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(roboparty_dexhand_sdk PRIVATE Threads::Threads)
rphand_runtime(roboparty_dexhand_sdk "$ORIGIN")
target_compile_options(roboparty_dexhand_sdk PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
option(RPHAND_BUILD_DEMO "Build dual-hand wave demo" ON)
if(RPHAND_BUILD_DEMO)
    add_executable(rphand_wave_demo examples/wave_demo.cpp)
    target_link_libraries(rphand_wave_demo PRIVATE roboparty::dexhand_sdk Threads::Threads)
    target_compile_options(rphand_wave_demo PRIVATE -Wall -Wextra -Wpedantic)
    rphand_runtime(rphand_wave_demo "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS rphand_wave_demo RUNTIME DESTINATION bin)
endif()
install(TARGETS roboparty_dexhand_sdk EXPORT roboparty_dexhand_sdkTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
include(CMakePackageConfigHelpers)
set(RPHAND_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand_sdk")
configure_package_config_file(cmake/roboparty_dexhand_sdkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhand_sdkConfig.cmake"
    INSTALL_DESTINATION "${RPHAND_CMAKE_INSTALL_DIR}" PATH_VARS CMAKE_INSTALL_LIBDIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhand_sdkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT roboparty_dexhand_sdkTargets NAMESPACE roboparty:: DESTINATION "${RPHAND_CMAKE_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhand_sdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhand_sdkConfigVersion.cmake"
    DESTINATION "${RPHAND_CMAKE_INSTALL_DIR}")
option(RPHAND_BUILD_SMOKE "Build a small hardware-free API/lifecycle check" OFF)
if(RPHAND_BUILD_SMOKE)
    enable_testing()
    foreach(test IN ITEMS basic_smoke canfd_backend session_state)
        add_executable(rphand_${test} tests/${test}.cpp)
        target_include_directories(rphand_${test} PRIVATE src)
        target_link_libraries(rphand_${test} PRIVATE roboparty::dexhand_sdk Threads::Threads)
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
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/examples.py $<TARGET_FILE:rphand_wave_demo>)
            set_tests_properties(rphand_examples PROPERTIES TIMEOUT 20)
        endif()
    endif()
endif()

if(ROBOPARTY_BUILD_PYTHON)
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_core python/bindings.cpp)
    target_link_libraries(_core PRIVATE roboparty::dexhand_sdk)
    rphand_runtime(_core "$ORIGIN/lib")
    install(TARGETS _core LIBRARY DESTINATION roboparty_dexhand_sdk COMPONENT python)
    install(DIRECTORY python/roboparty_dexhand_sdk/
        DESTINATION roboparty_dexhand_sdk COMPONENT python
        FILES_MATCHING PATTERN "*.py" PATTERN "*.pyi" PATTERN "py.typed")
    if(BUILD_SHARED_LIBS)
        install(TARGETS roboparty_dexhand_sdk
            LIBRARY DESTINATION roboparty_dexhand_sdk/lib COMPONENT python)
    endif()
endif()
