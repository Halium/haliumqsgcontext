include_directories(
    SYSTEM

    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
    ${ANDROID_INCLUDE_DIRS}
    ${DEVICEINFO_INCLUDE_DIRS}
    ${Qt5Gui_PRIVATE_INCLUDE_DIRS}
    ${Qt5Quick_PRIVATE_INCLUDE_DIRS}
)

add_library(
    haliumqsgcontext
    SHARED

    plugin.cpp
    context.cpp
    animationdriver.cpp
    rendercontext.cpp
    gralloctexture.cpp
    texturefactory.cpp
)

target_link_libraries(
    haliumqsgcontext

    ${EGL_LDFLAGS}
    ${EGL_LIBS}
    ${GLES_LDFLAGS}
    ${GLES_LIBS}
    -ldeviceinfo
    -lui
    Qt5::Core
    Qt5::DBus
    Qt5::Quick
    Qt5::Concurrent
)

set_property(TARGET haliumqsgcontext PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
install(TARGETS haliumqsgcontext LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt5/plugins/scenegraph")
