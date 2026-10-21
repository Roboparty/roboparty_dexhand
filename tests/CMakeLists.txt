cmake_minimum_required(VERSION 3.16)

# Configured directly, this directory verifies the installed SDK by compiling
# a real public example. No separate consumer project or duplicate main needed.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(rphand_install_check LANGUAGES CXX)
    find_package(rphand CONFIG REQUIRED)
    add_executable(rphand_install_check ../examples/cpp/read_state.cpp)
    target_link_libraries(rphand_install_check PRIVATE rphand::rphand)
    enable_testing()
    add_test(NAME installed_sdk COMMAND rphand_install_check --help)
    return()
endif()

# One suite per responsibility; none of these tests requires attached hardware.
# Keep tests beside the SDK DLL for Windows shared-library builds as well.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
foreach(test IN ITEMS transport session)
    add_executable(rphand_${test} ${test}.cpp)
    target_include_directories(rphand_${test} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(rphand_${test} PRIVATE rphand::rphand Threads::Threads)
    rphand_warnings(rphand_${test})
    add_test(NAME rphand_${test} COMMAND rphand_${test})
    set_tests_properties(rphand_${test} PROPERTIES TIMEOUT 20)
endforeach()

# Substitute the transport factory to exercise real Hand lifecycle behavior.
add_executable(rphand_hand_cleanup hand_cleanup.cpp
    ${PROJECT_SOURCE_DIR}/src/hand.cpp
    ${PROJECT_SOURCE_DIR}/src/rphand/protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/rphand/session.cpp)
target_include_directories(rphand_hand_cleanup PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_compile_features(rphand_hand_cleanup PRIVATE cxx_std_17)
target_link_libraries(rphand_hand_cleanup PRIVATE Threads::Threads)
rphand_warnings(rphand_hand_cleanup)
add_test(NAME rphand_hand_cleanup COMMAND rphand_hand_cleanup)
set_tests_properties(rphand_hand_cleanup PROPERTIES TIMEOUT 20)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    # This executable links a fake Hand, so valid example arguments cannot open USB/CAN.
    add_executable(rphand_example_driver example_driver.cpp)
    target_include_directories(rphand_example_driver PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_features(rphand_example_driver PRIVATE cxx_std_17)
    target_link_libraries(rphand_example_driver PRIVATE Threads::Threads)
    rphand_warnings(rphand_example_driver)
    add_test(NAME rphand_examples COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_python.py --examples $<TARGET_FILE:rphand_example_driver>)
    set_tests_properties(rphand_examples PROPERTIES TIMEOUT 30)
else()
    message(STATUS "Python3 not found: skipping cross-language example tests")
endif()
