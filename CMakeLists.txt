cmake_minimum_required(VERSION 3.9)
project(haliumqsgcontext)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include(FindPkgConfig)
find_package(Qt5Core 5.9 REQUIRED)
find_package(Qt5DBus 5.9 REQUIRED)
find_package(Qt5Gui 5.9 REQUIRED)
find_package(Qt5Qml 5.9 REQUIRED)
find_package(Qt5Quick 5.9 REQUIRED)
find_package(Qt5Concurrent 5.9 REQUIRED)

pkg_check_modules(EGL egl)
pkg_check_modules(GLES glesv2)
pkg_check_modules(ANDROID android-headers-28)
pkg_check_modules(DEVICEINFO deviceinfo)

# Disable all deprecated qt functions in and before qt version 5.9
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050900)

# Use the fast string builder
add_definitions(-DQT_USE_QSTRINGBUILDER)

# Standard install paths
include(GNUInstallDirs)

if (Qt5Gui_VERSION VERSION_LESS 5.14.0)
  _populate_Gui_plugin_properties("Gui" "PLATFORMS" "platforms")
else()
  _populate_Gui_plugin_properties("Gui" "PLATFORMS" "platforms" FALSE)
endif()
get_target_property(Qt5Gui_QPA_Plugin_Path Qt5::Gui IMPORTED_LOCATION_PLATFORMS)

add_definitions(-DQT_NO_KEYWORDS)
add_definitions(-DQT_NO_DEBUG_OUTPUT)

# add subdirectories to build
add_subdirectory(src)
