# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

function(add_dexhand_test name source)
  add_executable(${name} ${source})
  target_compile_features(${name} PRIVATE cxx_std_17)
  target_include_directories(
    ${name}
    PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/tests
            ${PROJECT_SOURCE_DIR}/thirdparty/include)
  target_link_libraries(${name} PRIVATE dexhand Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

add_dexhand_test(callback_gate test_callback_gate.cpp)
add_dexhand_test(factory_contract test_factory.cpp)
add_dexhand_test(feedback_period test_lhandpro_feedback_period.cpp)
target_link_libraries(feedback_period PRIVATE lhandpro_driver)
add_dexhand_test(lhandpro_lifecycle test_lhandpro_driver.cpp)

add_executable(lhandpro_config_cli_test test_lhandpro_config_cli.cpp)
target_compile_features(lhandpro_config_cli_test PRIVATE cxx_std_17)
target_include_directories(
  lhandpro_config_cli_test
  PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
          ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(lhandpro_config_cli_test
                      PRIVATE lhandpro_config_cli Threads::Threads)
add_test(NAME lhandpro_config_cli COMMAND lhandpro_config_cli_test)
set_tests_properties(lhandpro_config_cli PROPERTIES TIMEOUT 30)

add_executable(canfd_transport test_canfd_transport.cpp)
target_compile_features(canfd_transport PRIVATE cxx_std_17)
target_include_directories(canfd_transport PRIVATE ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(canfd_transport PRIVATE dexhand_canfd Threads::Threads)
add_test(NAME canfd_transport COMMAND canfd_transport)
set_tests_properties(canfd_transport PROPERTIES TIMEOUT 30)

add_executable(lhandpro_sdk test_lhandpro_sdk.cpp)
target_compile_features(lhandpro_sdk PRIVATE cxx_std_17)
target_include_directories(lhandpro_sdk PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(lhandpro_sdk PRIVATE lhandpro_driver)
add_test(NAME lhandpro_sdk COMMAND lhandpro_sdk)
set_tests_properties(lhandpro_sdk PROPERTIES TIMEOUT 30)

add_test(
  NAME pybind_api
  COMMAND
    ${CMAKE_COMMAND} -E env "PYTHONPATH=$<TARGET_FILE_DIR:dexhand_py>"
    "PYTHONNOUSERSITE=1" ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_pybind_api.py)
set_tests_properties(pybind_api PROPERTIES TIMEOUT 30)

add_test(NAME sdk_artifacts
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P
  ${CMAKE_CURRENT_SOURCE_DIR}/check_sdk_artifacts.cmake)

add_test(NAME logging_boundaries
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P
  ${CMAKE_CURRENT_SOURCE_DIR}/check_logging_boundaries.cmake)

add_test(
  NAME install_export
  COMMAND
    ${CMAKE_COMMAND}
    -DBUILD_DIR=${PROJECT_BINARY_DIR}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DPACKAGE_VERSION=${PROJECT_VERSION}
    -DCHECK_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/check_install_export.cmake
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_install_export_test.cmake)
set_tests_properties(install_export PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

if(DEXHAND_ENABLE_VCAN_TESTS)
  add_executable(vcan_two_socket test_vcan_two_socket.cpp)
  target_compile_features(vcan_two_socket PRIVATE cxx_std_17)
  target_include_directories(vcan_two_socket PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(vcan_two_socket PRIVATE dexhand_canfd Threads::Threads)
  add_test(NAME vcan_two_socket COMMAND vcan_two_socket vcan-dexhand0)
  add_test(NAME vcan_observation_state COMMAND vcan_two_socket --self-test)
  set_tests_properties(vcan_two_socket PROPERTIES TIMEOUT 15)
  set_tests_properties(vcan_observation_state PROPERTIES TIMEOUT 15)
endif()
