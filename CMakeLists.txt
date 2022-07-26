cmake_minimum_required(VERSION 3.1)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(CMAKE_CXX_STANDARD 14)

include(FindPkgConfig)
find_package(Qt5Core 5.9 REQUIRED)
find_package(Qt5DBus 5.9 REQUIRED)
find_package(Qt5Gui 5.9 REQUIRED)
find_package(Qt5Qml 5.9 REQUIRED)
find_package(Qt5Quick 5.9 REQUIRED)

pkg_check_modules(EGL egl)
pkg_check_modules(GLES glesv2)
pkg_check_modules(ANDROID android-headers-28)

# Disable all deprecated qt functions in and before qt version 5.9
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050900)

# Use the fast string builder
add_definitions(-DQT_USE_QSTRINGBUILDER)

# Standard install paths
include(GNUInstallDirs)

# Determine QPA plugin install path
# FIXME(greyback) there must be a better way than calling a private macro to set a property.
# I'm manually calling a macro from Qt5GuiConfig.cmake, but I don't understand why it isn't being called.
if (Qt5Gui_VERSION VERSION_LESS 5.14.0)
  _populate_Gui_plugin_properties("Gui" "PLATFORMS" "platforms")
else()
  _populate_Gui_plugin_properties("Gui" "PLATFORMS" "platforms" FALSE)
endif()
get_target_property(Qt5Gui_QPA_Plugin_Path Qt5::Gui IMPORTED_LOCATION_PLATFORMS)

# Mir uses boost, which does not like Qt defining macros named "signals" and "slots"
add_definitions(-DQT_NO_KEYWORDS)

# add subdirectories to build
add_subdirectory(src)
