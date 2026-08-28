# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

cmake_minimum_required(VERSION 3.15)

foreach(required IN ITEMS
        BUILD_DIR SOURCE_DIR PYTHON_EXECUTABLE CHECK_SCRIPT PACKAGE_VERSION)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

foreach(directory_variable IN ITEMS BUILD_DIR SOURCE_DIR)
  get_filename_component(${directory_variable}_ABS
    "${${directory_variable}}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${${directory_variable}_ABS}")
    message(FATAL_ERROR
      "${directory_variable} is not an existing directory: "
      "${${directory_variable}_ABS}")
  endif()
  get_filename_component(${directory_variable}_REAL
    "${${directory_variable}_ABS}" REALPATH)
endforeach()

get_filename_component(PYTHON_EXECUTABLE_ABS
  "${PYTHON_EXECUTABLE}" ABSOLUTE)
if(NOT EXISTS "${PYTHON_EXECUTABLE_ABS}" OR
   IS_DIRECTORY "${PYTHON_EXECUTABLE_ABS}")
  message(FATAL_ERROR
    "PYTHON_EXECUTABLE is not an existing file: ${PYTHON_EXECUTABLE_ABS}")
endif()

get_filename_component(CHECK_SCRIPT_ABS "${CHECK_SCRIPT}" ABSOLUTE)
if(NOT EXISTS "${CHECK_SCRIPT_ABS}" OR IS_DIRECTORY "${CHECK_SCRIPT_ABS}")
  message(FATAL_ERROR "CHECK_SCRIPT is not an existing file: ${CHECK_SCRIPT_ABS}")
endif()

function(paths_are_disjoint path_a path_b output_variable)
  if("${path_a}" STREQUAL "${path_b}")
    set(${output_variable} FALSE PARENT_SCOPE)
    return()
  endif()

  set(path_a_with_separator "${path_a}/")
  set(path_b_with_separator "${path_b}/")
  string(FIND "${path_a_with_separator}" "${path_b_with_separator}"
    path_a_within_path_b)
  string(FIND "${path_b_with_separator}" "${path_a_with_separator}"
    path_b_within_path_a)
  if(path_a_within_path_b EQUAL 0 OR path_b_within_path_a EQUAL 0)
    set(${output_variable} FALSE PARENT_SCOPE)
  else()
    set(${output_variable} TRUE PARENT_SCOPE)
  endif()
endfunction()

set(temp_candidates)
foreach(environment_variable IN ITEMS RUNNER_TEMP TMPDIR)
  if(DEFINED ENV{${environment_variable}} AND
     NOT "$ENV{${environment_variable}}" STREQUAL "")
    list(APPEND temp_candidates "$ENV{${environment_variable}}")
  endif()
endforeach()
list(APPEND temp_candidates "/tmp")
list(REMOVE_DUPLICATES temp_candidates)

set(EXTERNAL_TEMP_BASE "")
foreach(candidate IN LISTS temp_candidates)
  get_filename_component(candidate_abs "${candidate}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${candidate_abs}")
    continue()
  endif()
  get_filename_component(candidate_real "${candidate_abs}" REALPATH)
  paths_are_disjoint("${candidate_real}" "${SOURCE_DIR_REAL}"
    disjoint_from_source)
  paths_are_disjoint("${candidate_real}" "${BUILD_DIR_REAL}"
    disjoint_from_build)
  if(disjoint_from_source AND disjoint_from_build)
    set(EXTERNAL_TEMP_BASE "${candidate_real}")
    break()
  endif()
endforeach()
if(EXTERNAL_TEMP_BASE STREQUAL "")
  message(FATAL_ERROR
    "no external temporary directory is disjoint from source and build")
endif()

set(TEMP_ROOT "")
foreach(attempt RANGE 1 10)
  string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef unique_suffix)
  set(candidate_root
    "${EXTERNAL_TEMP_BASE}/roboparty-dexhand-install-export-${unique_suffix}")
  if(NOT EXISTS "${candidate_root}" AND NOT IS_SYMLINK "${candidate_root}")
    file(MAKE_DIRECTORY "${candidate_root}")
    set(TEMP_ROOT "${candidate_root}")
    break()
  endif()
endforeach()
if(TEMP_ROOT STREQUAL "" OR NOT IS_DIRECTORY "${TEMP_ROOT}")
  message(FATAL_ERROR
    "failed to create a unique install/export temporary directory")
endif()

set(PREFIX "${TEMP_ROOT}/prefix")
if(EXISTS "${PREFIX}" OR IS_SYMLINK "${PREFIX}")
  message(FATAL_ERROR "generated install prefix already exists: ${PREFIX}")
endif()

string(SHA256 PREFIX_HASH "${PREFIX}")
set(CHECK_SCRATCH "${BUILD_DIR_REAL}/install-export-${PREFIX_HASH}")
if(EXISTS "${CHECK_SCRATCH}" OR IS_SYMLINK "${CHECK_SCRATCH}")
  message(FATAL_ERROR
    "generated install/export scratch already exists: ${CHECK_SCRATCH}")
endif()

get_filename_component(CMAKE_COMMAND_DIR "${CMAKE_COMMAND}" DIRECTORY)
set(CLEAN_PATH "${CMAKE_COMMAND_DIR}:/usr/bin:/bin")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    --unset=LD_LIBRARY_PATH
    --unset=CMAKE_PREFIX_PATH
    --unset=roboparty_dexhand_DIR
    --unset=DESTDIR
    "PATH=${CLEAN_PATH}"
    "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${BUILD_DIR_REAL}"
    "-DSOURCE_DIR=${SOURCE_DIR_REAL}"
    "-DPREFIX=${PREFIX}"
    "-DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE_ABS}"
    "-DPACKAGE_VERSION=${PACKAGE_VERSION}"
    -P "${CHECK_SCRIPT_ABS}"
  RESULT_VARIABLE check_rc
  OUTPUT_VARIABLE check_out
  ERROR_VARIABLE check_err)

if(NOT check_rc EQUAL 0)
  message(FATAL_ERROR
    "install/export gate failed; evidence retained in ${TEMP_ROOT} and "
    "${CHECK_SCRATCH}:\n${check_out}\n${check_err}")
endif()

file(REMOVE_RECURSE "${TEMP_ROOT}" "${CHECK_SCRATCH}")
if(EXISTS "${TEMP_ROOT}" OR IS_SYMLINK "${TEMP_ROOT}" OR
   EXISTS "${CHECK_SCRATCH}" OR IS_SYMLINK "${CHECK_SCRATCH}")
  message(FATAL_ERROR
    "install/export gate passed but precise scratch cleanup failed: "
    "${TEMP_ROOT}; ${CHECK_SCRATCH}")
endif()
