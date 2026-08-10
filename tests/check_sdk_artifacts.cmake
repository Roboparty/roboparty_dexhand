cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(X86_LIB "${SOURCE_DIR}/thirdparty/lib/x86_64/libLHandProLib.so")
set(ARM_LIB "${SOURCE_DIR}/thirdparty/lib/aarch64/libLHandProLib.so")
set(VENDOR_HEADER "${SOURCE_DIR}/thirdparty/include/LHandProLib/LHandProLib.h")

foreach(path IN ITEMS "${X86_LIB}" "${ARM_LIB}" "${VENDOR_HEADER}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing vendor artifact: ${path}")
  endif()
endforeach()

file(SHA256 "${X86_LIB}" X86_SHA)
file(SHA256 "${ARM_LIB}" ARM_SHA)
if(NOT X86_SHA STREQUAL "3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640")
  message(FATAL_ERROR "Unexpected x86-64 SDK hash: ${X86_SHA}")
endif()
if(NOT ARM_SHA STREQUAL "476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044")
  message(FATAL_ERROR "Unexpected AArch64 SDK hash: ${ARM_SHA}")
endif()

execute_process(COMMAND readelf -h "${X86_LIB}" OUTPUT_VARIABLE X86_ELF
                RESULT_VARIABLE X86_RC)
execute_process(COMMAND readelf -h "${ARM_LIB}" OUTPUT_VARIABLE ARM_ELF
                RESULT_VARIABLE ARM_RC)
if(NOT X86_RC EQUAL 0 OR NOT X86_ELF MATCHES "Machine:[ ]+Advanced Micro Devices X86-64")
  message(FATAL_ERROR "x86-64 SDK has the wrong ELF machine")
endif()
if(NOT ARM_RC EQUAL 0 OR NOT ARM_ELF MATCHES "Machine:[ ]+AArch64")
  message(FATAL_ERROR "AArch64 SDK has the wrong ELF machine")
endif()

foreach(case IN ITEMS "x86_64:x86_64" "amd64:x86_64"
                      "aarch64:aarch64" "arm64:aarch64")
  string(REPLACE ":" ";" pair "${case}")
  list(GET pair 0 input_arch)
  list(GET pair 1 expected_arch)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -DSOURCE_DIR=${SOURCE_DIR} -DARCH=${input_arch}
      -DEXPECTED=${expected_arch}
      -P ${SOURCE_DIR}/tests/check_arch_value.cmake
    RESULT_VARIABLE alias_rc)
  if(NOT alias_rc EQUAL 0)
    message(FATAL_ERROR "Architecture alias failed: ${case}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DSOURCE_DIR=${SOURCE_DIR} -DARCH=riscv64 -DEXPECT_FAILURE=ON
    -P ${SOURCE_DIR}/tests/check_arch_value.cmake
  RESULT_VARIABLE unsupported_rc)
if(unsupported_rc EQUAL 0)
  message(FATAL_ERROR "Unsupported architecture was accepted")
endif()
