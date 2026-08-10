function(roboparty_dexhand_normalize_arch input_arch output_var)
  string(TOLOWER "${input_arch}" normalized_arch)
  if(normalized_arch STREQUAL "x86_64" OR normalized_arch STREQUAL "amd64")
    set(selected_arch "x86_64")
  elseif(normalized_arch STREQUAL "aarch64" OR normalized_arch STREQUAL "arm64")
    set(selected_arch "aarch64")
  else()
    message(FATAL_ERROR
      "Unsupported roboparty_dexhand architecture '${input_arch}'. "
      "Supported values: x86_64, amd64, aarch64, arm64")
  endif()
  set(${output_var} "${selected_arch}" PARENT_SCOPE)
endfunction()
