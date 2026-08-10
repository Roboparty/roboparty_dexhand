# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

# LHandPro driver static library.
# Links against libLHandProLib.so (closed-source SDK) and motors_canfd.

AUX_SOURCE_DIRECTORY(. SOURCE_LIST_LHANDPRO)

add_library(lhandpro_driver STATIC ${SOURCE_LIST_LHANDPRO})

target_include_directories(lhandpro_driver
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Add roboparty_motors include paths (for canfd_iso.hpp)
if(MOTORS_INCLUDE_DIRS)
    target_include_directories(lhandpro_driver PRIVATE ${MOTORS_INCLUDE_DIRS})
endif()

# Link against roboparty_motors CANFD transport (provides MotorsCANFD)
if(DEFINED ROBOPARTY_MOTORS_SOURCE_DIR OR DEFINED MOTORS_PREBUILT_DIR)
    # Source tree or pre-built: include paths already set by parent CMake
    target_link_libraries(lhandpro_driver PUBLIC ${PUBLIC_DEPENDENCIES} motors_canfd)
else()
    target_link_libraries(lhandpro_driver PUBLIC ${PUBLIC_DEPENDENCIES}
        roboparty_motors::motors_canfd)
endif()

# Link against the closed-source LHandProLib SDK
# Use absolute path to the .so (avoids link_directories issues)
target_link_libraries(lhandpro_driver PUBLIC
    ${PROJECT_SOURCE_DIR}/thirdparty/lib/libLHandProLib.so)
