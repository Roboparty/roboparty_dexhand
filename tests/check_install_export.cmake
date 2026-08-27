# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

cmake_minimum_required(VERSION 3.15)

foreach(required IN ITEMS BUILD_DIR SOURCE_DIR PREFIX PYTHON_EXECUTABLE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

function(normalize_existing_directory input_name lexical_output real_output)
  get_filename_component(candidate_abs "${${input_name}}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${candidate_abs}")
    message(FATAL_ERROR "${input_name} is not an existing directory: ${candidate_abs}")
  endif()
  get_filename_component(candidate_real "${candidate_abs}" REALPATH)
  set(${lexical_output} "${candidate_abs}" PARENT_SCOPE)
  set(${real_output} "${candidate_real}" PARENT_SCOPE)
endfunction()

function(read_unique_cache_value cache_file entry_name output_name)
  file(STRINGS "${cache_file}" cache_entries
    REGEX "^${entry_name}(:[^=]*)?=")
  list(LENGTH cache_entries cache_entry_count)
  if(NOT cache_entry_count EQUAL 1)
    message(FATAL_ERROR
      "expected one ${entry_name} cache entry, got ${cache_entry_count}")
  endif()
  list(GET cache_entries 0 cache_entry)
  string(REGEX REPLACE "^[^=]*=" "" cache_value "${cache_entry}")
  set(${output_name} "${cache_value}" PARENT_SCOPE)
endfunction()

function(assert_disjoint path_a path_b label)
  if("${path_a}" STREQUAL "${path_b}")
    message(FATAL_ERROR
      "install prefix must be disjoint from source and build directories (${label})")
  endif()

  set(path_a_with_separator "${path_a}/")
  set(path_b_with_separator "${path_b}/")
  string(FIND "${path_a_with_separator}" "${path_b_with_separator}"
    path_a_within_path_b)
  string(FIND "${path_b_with_separator}" "${path_a_with_separator}"
    path_b_within_path_a)
  if(path_a_within_path_b EQUAL 0 OR path_b_within_path_a EQUAL 0)
    message(FATAL_ERROR
      "install prefix must be disjoint from source and build directories (${label})")
  endif()
endfunction()

normalize_existing_directory(SOURCE_DIR SOURCE_DIR_LEXICAL SOURCE_DIR_REAL)
normalize_existing_directory(BUILD_DIR BUILD_DIR_LEXICAL BUILD_DIR_REAL)
if(SOURCE_DIR_REAL STREQUAL BUILD_DIR_REAL)
  message(FATAL_ERROR "source and build directories must differ")
endif()

set(CACHE_FILE "${BUILD_DIR_LEXICAL}/CMakeCache.txt")
if(NOT EXISTS "${CACHE_FILE}")
  message(FATAL_ERROR "build cache is missing: ${CACHE_FILE}")
endif()
read_unique_cache_value("${CACHE_FILE}" CMAKE_HOME_DIRECTORY cache_home_value)
get_filename_component(CACHE_HOME_LEXICAL "${cache_home_value}" ABSOLUTE)
if(NOT IS_DIRECTORY "${CACHE_HOME_LEXICAL}")
  message(FATAL_ERROR
    "cached CMake source is not an existing directory: ${CACHE_HOME_LEXICAL}")
endif()
get_filename_component(CACHE_HOME_REAL "${CACHE_HOME_LEXICAL}" REALPATH)
if(NOT SOURCE_DIR_REAL STREQUAL CACHE_HOME_REAL)
  message(FATAL_ERROR
    "SOURCE_DIR does not match the configured CMake source: "
    "${SOURCE_DIR_LEXICAL} != ${CACHE_HOME_LEXICAL}")
endif()

read_unique_cache_value("${CACHE_FILE}" CMAKE_CACHEFILE_DIR cache_build_value)
get_filename_component(CACHE_BUILD_LEXICAL "${cache_build_value}" ABSOLUTE)
if(NOT IS_DIRECTORY "${CACHE_BUILD_LEXICAL}")
  message(FATAL_ERROR
    "cached CMake build is not an existing directory: ${CACHE_BUILD_LEXICAL}")
endif()
get_filename_component(CACHE_BUILD_REAL "${CACHE_BUILD_LEXICAL}" REALPATH)
if(NOT BUILD_DIR_REAL STREQUAL CACHE_BUILD_REAL)
  message(FATAL_ERROR
    "BUILD_DIR does not match the configured CMake build: "
    "${BUILD_DIR_LEXICAL} != ${CACHE_BUILD_LEXICAL}")
endif()

get_filename_component(PYTHON_EXECUTABLE_ABS "${PYTHON_EXECUTABLE}" ABSOLUTE)
if(NOT EXISTS "${PYTHON_EXECUTABLE_ABS}" OR
  IS_DIRECTORY "${PYTHON_EXECUTABLE_ABS}")
  message(FATAL_ERROR
    "PYTHON_EXECUTABLE is not an existing file: ${PYTHON_EXECUTABLE_ABS}")
endif()
get_filename_component(PYTHON_EXECUTABLE_REAL
  "${PYTHON_EXECUTABLE_ABS}" REALPATH)

get_filename_component(PREFIX_ABS "${PREFIX}" ABSOLUTE)
if(EXISTS "${PREFIX_ABS}" OR IS_SYMLINK "${PREFIX_ABS}")
  message(FATAL_ERROR "install prefix already exists: ${PREFIX_ABS}")
endif()
get_filename_component(PREFIX_PARENT "${PREFIX_ABS}" DIRECTORY)
get_filename_component(PREFIX_NAME "${PREFIX_ABS}" NAME)
if(PREFIX_NAME STREQUAL "" OR NOT IS_DIRECTORY "${PREFIX_PARENT}")
  message(FATAL_ERROR
    "install prefix must have an existing parent directory: ${PREFIX_ABS}")
endif()
get_filename_component(PREFIX_PARENT_REAL "${PREFIX_PARENT}" REALPATH)
set(PREFIX_REAL "${PREFIX_PARENT_REAL}/${PREFIX_NAME}")
if(EXISTS "${PREFIX_REAL}" OR IS_SYMLINK "${PREFIX_REAL}")
  message(FATAL_ERROR "normalized install prefix already exists: ${PREFIX_REAL}")
endif()

set(RELOCATED "${PREFIX_REAL}-relocated")
if(EXISTS "${RELOCATED}" OR IS_SYMLINK "${RELOCATED}")
  message(FATAL_ERROR "relocation destination already exists: ${RELOCATED}")
endif()

assert_disjoint("${PREFIX_REAL}" "${SOURCE_DIR_REAL}" "source")
assert_disjoint("${PREFIX_REAL}" "${BUILD_DIR_REAL}" "build")

if(DEFINED ENV{DESTDIR} AND NOT "$ENV{DESTDIR}" STREQUAL "")
  message(FATAL_ERROR "DESTDIR must be unset for the install/export gate")
endif()

read_unique_cache_value("${CACHE_FILE}" CMAKE_INSTALL_LIBDIR INSTALL_LIBDIR)
if(INSTALL_LIBDIR STREQUAL "" OR IS_ABSOLUTE "${INSTALL_LIBDIR}")
  message(FATAL_ERROR
    "CMAKE_INSTALL_LIBDIR must be a non-empty relative path: ${INSTALL_LIBDIR}")
endif()
string(FIND "${INSTALL_LIBDIR}" ";" install_libdir_semicolon)
string(FIND "${INSTALL_LIBDIR}" "\\" install_libdir_backslash)
if(NOT install_libdir_semicolon EQUAL -1 OR
  NOT install_libdir_backslash EQUAL -1)
  message(FATAL_ERROR
    "CMAKE_INSTALL_LIBDIR contains an unsafe separator: ${INSTALL_LIBDIR}")
endif()
string(REPLACE "/" ";" install_libdir_parts "${INSTALL_LIBDIR}")
foreach(part IN LISTS install_libdir_parts)
  if(part STREQUAL "" OR part STREQUAL "." OR part STREQUAL "..")
    message(FATAL_ERROR
      "CMAKE_INSTALL_LIBDIR contains an unsafe component: ${INSTALL_LIBDIR}")
  endif()
endforeach()

read_unique_cache_value("${CACHE_FILE}" CMAKE_INSTALL_BINDIR INSTALL_BINDIR)
if(INSTALL_BINDIR STREQUAL "" OR IS_ABSOLUTE "${INSTALL_BINDIR}")
  message(FATAL_ERROR
    "CMAKE_INSTALL_BINDIR must be a non-empty relative path: ${INSTALL_BINDIR}")
endif()
string(FIND "${INSTALL_BINDIR}" ";" install_bindir_semicolon)
string(FIND "${INSTALL_BINDIR}" "\\" install_bindir_backslash)
if(NOT install_bindir_semicolon EQUAL -1 OR
  NOT install_bindir_backslash EQUAL -1)
  message(FATAL_ERROR
    "CMAKE_INSTALL_BINDIR contains an unsafe separator: ${INSTALL_BINDIR}")
endif()
string(REPLACE "/" ";" install_bindir_parts "${INSTALL_BINDIR}")
foreach(part IN LISTS install_bindir_parts)
  if(part STREQUAL "" OR part STREQUAL "." OR part STREQUAL "..")
    message(FATAL_ERROR
      "CMAKE_INSTALL_BINDIR contains an unsafe component: ${INSTALL_BINDIR}")
  endif()
endforeach()

if(NOT EXISTS "${BUILD_DIR_REAL}/cmake_install.cmake")
  message(FATAL_ERROR "build directory is not installable: ${BUILD_DIR_REAL}")
endif()
if(NOT EXISTS "${SOURCE_DIR_REAL}/tests/installed_consumer/CMakeLists.txt")
  message(FATAL_ERROR "installed consumer source is missing")
endif()

string(SHA256 PREFIX_HASH "${PREFIX_REAL}")
set(SCRATCH_ROOT "${BUILD_DIR_REAL}/install-export-${PREFIX_HASH}")
if(EXISTS "${SCRATCH_ROOT}" OR IS_SYMLINK "${SCRATCH_ROOT}")
  message(FATAL_ERROR
    "install/export scratch root already exists: ${SCRATCH_ROOT}")
endif()
set(CONSUMER_BUILD "${SCRATCH_ROOT}/consumer")
set(VERSION_SOURCE "${SCRATCH_ROOT}/version-source")
set(VERSION_REJECT_BUILD "${SCRATCH_ROOT}/version-0.2")
set(VERSION_ACCEPT_BUILD "${SCRATCH_ROOT}/version-0.3")

execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR_REAL}"
                --prefix "${PREFIX_REAL}" RESULT_VARIABLE install_rc)
if(NOT install_rc EQUAL 0)
  message(FATAL_ERROR "install failed: ${install_rc}")
endif()

set(INSTALL_LIB_ROOT "${PREFIX_REAL}/${INSTALL_LIBDIR}")
set(CONFIG_TOOL "${PREFIX_REAL}/${INSTALL_BINDIR}/roboparty-dexhand-config")
set(PACKAGE_DIR "${INSTALL_LIB_ROOT}/cmake/roboparty_dexhand")
set(TARGET_FILE "${PACKAGE_DIR}/roboparty_dexhandTargets.cmake")
if(NOT EXISTS "${CONFIG_TOOL}" OR IS_DIRECTORY "${CONFIG_TOOL}")
  message(FATAL_ERROR "installed feedback configuration tool is missing")
endif()
if(NOT IS_DIRECTORY "${PACKAGE_DIR}")
  message(FATAL_ERROR "installed CMake package directory is missing: ${PACKAGE_DIR}")
endif()
if(NOT EXISTS "${TARGET_FILE}")
  message(FATAL_ERROR "installed target file is missing: ${TARGET_FILE}")
endif()

file(GLOB configs "${PACKAGE_DIR}/*.cmake")
if(NOT configs)
  message(FATAL_ERROR "installed CMake package files are missing")
endif()
set(forbidden_config_paths
    "${SOURCE_DIR_LEXICAL}"
    "${SOURCE_DIR_REAL}"
    "${CACHE_HOME_LEXICAL}"
    "${CACHE_HOME_REAL}"
    "${BUILD_DIR_LEXICAL}"
    "${BUILD_DIR_REAL}"
    "${CACHE_BUILD_LEXICAL}"
    "${CACHE_BUILD_REAL}")
list(REMOVE_DUPLICATES forbidden_config_paths)
foreach(config IN LISTS configs)
  file(READ "${config}" content)
  foreach(forbidden_path IN LISTS forbidden_config_paths)
    string(FIND "${content}" "${forbidden_path}" forbidden_path_index)
    if(NOT forbidden_path_index EQUAL -1)
      message(FATAL_ERROR
        "source/build path ${forbidden_path} leaked through ${config}")
    endif()
  endforeach()
  foreach(private_name IN ITEMS
          roboparty_motors MotorsCANFD motors_canfd
          lhandpro_driver dexhand_canfd lhandpro_sdk)
    string(FIND "${content}" "${private_name}" private_name_index)
    if(NOT private_name_index EQUAL -1)
      message(FATAL_ERROR "private dependency leaked through ${config}")
    endif()
  endforeach()
endforeach()

if(EXISTS "${PREFIX_REAL}/include/LHandProLib/LHandProLib.h" OR
  EXISTS "${PREFIX_REAL}/include/protocol/canfd_transport.hpp")
  message(FATAL_ERROR "private header was installed")
endif()

set(DEXHAND_LIBRARY "${INSTALL_LIB_ROOT}/libdexhand.so")
set(SDK_LIBRARY "${INSTALL_LIB_ROOT}/libLHandProLib.so")
if(NOT EXISTS "${DEXHAND_LIBRARY}" OR NOT EXISTS "${SDK_LIBRARY}")
  message(FATAL_ERROR "required shared runtime missing")
endif()

file(GLOB_RECURSE installed_paths "${PREFIX_REAL}/*")
set(sdk_copies)
foreach(installed_path IN LISTS installed_paths)
  get_filename_component(installed_name "${installed_path}" NAME)
  if(installed_name STREQUAL "libLHandProLib.so")
    list(APPEND sdk_copies "${installed_path}")
  endif()
endforeach()
list(LENGTH sdk_copies sdk_count)
if(NOT sdk_count EQUAL 1)
  message(FATAL_ERROR "expected exactly one selected SDK, got ${sdk_count}")
endif()

file(READ "${TARGET_FILE}" target_content)
string(FIND "${target_content}" "roboparty_dexhand::dexhand" public_target_index)
if(public_target_index EQUAL -1)
  message(FATAL_ERROR "public dexhand target missing")
endif()

file(RENAME "${PREFIX_REAL}" "${RELOCATED}")

set(RELOCATED_LIB_ROOT "${RELOCATED}/${INSTALL_LIBDIR}")
set(RELOCATED_PACKAGE_DIR
    "${RELOCATED_LIB_ROOT}/cmake/roboparty_dexhand")
set(RELOCATED_TARGET_FILE
    "${RELOCATED_PACKAGE_DIR}/roboparty_dexhandTargets.cmake")
if(NOT IS_DIRECTORY "${RELOCATED_PACKAGE_DIR}")
  message(FATAL_ERROR
    "relocated CMake package directory is missing: ${RELOCATED_PACKAGE_DIR}")
endif()
if(NOT EXISTS "${RELOCATED_TARGET_FILE}")
  message(FATAL_ERROR
    "relocated target file is missing: ${RELOCATED_TARGET_FILE}")
endif()

set(RELOCATED_CONFIG_TOOL
  "${RELOCATED}/${INSTALL_BINDIR}/roboparty-dexhand-config")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
    "${RELOCATED_CONFIG_TOOL}" --help
  WORKING_DIRECTORY "/tmp"
  RESULT_VARIABLE config_help_rc
  OUTPUT_VARIABLE config_help_out
  ERROR_VARIABLE config_help_err)
if(NOT config_help_rc EQUAL 0 OR
  NOT config_help_out MATCHES "feedback-period")
  message(FATAL_ERROR
    "relocated config tool failed: ${config_help_out}${config_help_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    --unset=CMAKE_PREFIX_PATH
    --unset=roboparty_dexhand_DIR
    "${CMAKE_COMMAND}"
    -S "${SOURCE_DIR_REAL}/tests/installed_consumer"
    -B "${CONSUMER_BUILD}"
    "-DCMAKE_PREFIX_PATH=${RELOCATED}"
    "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON"
    "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON"
  RESULT_VARIABLE configure_rc OUTPUT_VARIABLE configure_out
  ERROR_VARIABLE configure_err)
if(NOT configure_rc EQUAL 0)
  message(FATAL_ERROR
    "installed consumer configure failed:\n${configure_out}\n${configure_err}")
endif()

set(CONSUMER_CACHE "${CONSUMER_BUILD}/CMakeCache.txt")
read_unique_cache_value(
  "${CONSUMER_CACHE}" roboparty_dexhand_DIR consumer_package_dir_value)
get_filename_component(CONSUMER_PACKAGE_DIR_LEXICAL
  "${consumer_package_dir_value}" ABSOLUTE)
if(NOT IS_DIRECTORY "${CONSUMER_PACKAGE_DIR_LEXICAL}")
  message(FATAL_ERROR
    "consumer resolved a missing package: ${CONSUMER_PACKAGE_DIR_LEXICAL}")
endif()
get_filename_component(CONSUMER_PACKAGE_DIR_REAL
  "${CONSUMER_PACKAGE_DIR_LEXICAL}" REALPATH)
get_filename_component(RELOCATED_PACKAGE_DIR_REAL
  "${RELOCATED_PACKAGE_DIR}" REALPATH)
set(consumer_package_with_separator "${CONSUMER_PACKAGE_DIR_REAL}/")
set(relocated_package_with_separator "${RELOCATED_PACKAGE_DIR_REAL}/")
string(FIND "${consumer_package_with_separator}"
  "${relocated_package_with_separator}" consumer_package_index)
if(NOT consumer_package_index EQUAL 0)
  message(FATAL_ERROR
    "consumer resolved package outside relocated prefix: "
    "${CONSUMER_PACKAGE_DIR_LEXICAL}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BUILD}"
  RESULT_VARIABLE build_rc OUTPUT_VARIABLE build_out ERROR_VARIABLE build_err)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR
    "installed consumer build failed:\n${build_out}\n${build_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
    "${CONSUMER_BUILD}/dexhand_consumer"
  RESULT_VARIABLE consumer_rc OUTPUT_VARIABLE consumer_out
  ERROR_VARIABLE consumer_err)
if(NOT consumer_rc EQUAL 0)
  message(FATAL_ERROR
    "installed consumer run failed:\n${consumer_out}\n${consumer_err}")
endif()

file(MAKE_DIRECTORY "${VERSION_SOURCE}")
file(WRITE "${VERSION_SOURCE}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.15)
project(dexhand_version_consumer LANGUAGES CXX)
if(NOT DEFINED REQUESTED_VERSION)
  message(FATAL_ERROR "REQUESTED_VERSION is required")
endif()
find_package(roboparty_dexhand ${REQUESTED_VERSION} CONFIG REQUIRED)
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${VERSION_SOURCE}"
    -B "${VERSION_REJECT_BUILD}"
    "-DCMAKE_PREFIX_PATH=${RELOCATED}"
    "-Droboparty_dexhand_DIR=${RELOCATED_PACKAGE_DIR}"
    "-DREQUESTED_VERSION=0.2"
  RESULT_VARIABLE version_reject_rc OUTPUT_VARIABLE version_reject_out
  ERROR_VARIABLE version_reject_err)
if(version_reject_rc EQUAL 0)
  message(FATAL_ERROR "requested 0.2 unexpectedly succeeded")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${VERSION_SOURCE}"
    -B "${VERSION_ACCEPT_BUILD}"
    "-DCMAKE_PREFIX_PATH=${RELOCATED}"
    "-Droboparty_dexhand_DIR=${RELOCATED_PACKAGE_DIR}"
    "-DREQUESTED_VERSION=0.3"
  RESULT_VARIABLE version_accept_rc OUTPUT_VARIABLE version_accept_out
  ERROR_VARIABLE version_accept_err)
if(NOT version_accept_rc EQUAL 0)
  message(FATAL_ERROR
    "requested 0.3 failed:\n${version_accept_out}\n${version_accept_err}")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE_REAL}" -c
    "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
  RESULT_VARIABLE py_version_rc OUTPUT_VARIABLE PY_VERSION
  ERROR_VARIABLE py_version_err OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT py_version_rc EQUAL 0)
  message(FATAL_ERROR "Python version lookup failed: ${py_version_err}")
endif()
set(PYTHON_SITE
    "${RELOCATED_LIB_ROOT}/python${PY_VERSION}/site-packages")
file(GLOB PYTHON_MODULE "${PYTHON_SITE}/dexhand_py*.so")
list(LENGTH PYTHON_MODULE module_count)
if(NOT module_count EQUAL 1)
  message(FATAL_ERROR "expected one installed dexhand_py module, got ${module_count}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
    "PYTHONNOUSERSITE=1" "PYTHONPATH=${PYTHON_SITE}"
    "${PYTHON_EXECUTABLE_REAL}" "${SOURCE_DIR_REAL}/tests/test_pybind_api.py"
  WORKING_DIRECTORY "/tmp"
  RESULT_VARIABLE python_rc OUTPUT_VARIABLE python_out ERROR_VARIABLE python_err)
if(NOT python_rc EQUAL 0)
  message(FATAL_ERROR
    "installed Python API failed:\n${python_out}\n${python_err}")
endif()
