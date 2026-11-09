cmake_minimum_required(VERSION 3.21)
project(KWinWindowTabsInput LANGUAGES C CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS DBus Qml Widgets)
find_package(KWin 6.7.6 EXACT REQUIRED CONFIG)
find_package(Wayland REQUIRED Client)
find_package(PkgConfig REQUIRED)
pkg_get_variable(wayland_protocols_data wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

qt_policy(SET QTP0001 NEW)

set(input_module_dir "${CMAKE_CURRENT_BINARY_DIR}/org/kde/kwinwindowtabs/input")
qt_add_qml_module(kwinwindowtabsinput
    URI org.kde.kwinwindowtabs.input
    VERSION 1.0
    PLUGIN_TARGET kwinwindowtabsinput
    OUTPUT_DIRECTORY "${input_module_dir}"
    SOURCES native/moveinput.h native/moveinput.cpp native/groupwindow.h native/groupwindow.cpp
)
target_include_directories(kwinwindowtabsinput PRIVATE native "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(kwinwindowtabsinput PRIVATE KWin::kwin Qt6::Qml Wayland::Client)

function(add_wayland_protocol name protocol_dir qt_dir)
    find_file(${name}_xml "${name}.xml"
        HINTS "${wayland_protocols_data}/${protocol_dir}"
        PATHS "/usr/share/wayland-protocols/${protocol_dir}" "/usr/share/qt6/wayland/protocols/${qt_dir}"
        REQUIRED)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${name}-client.h")
    set(code "${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c")
    add_custom_command(OUTPUT "${header}" "${code}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${${name}_xml}" "${header}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${${name}_xml}" "${code}"
        DEPENDS "${${name}_xml}"
        VERBATIM)
    target_sources(kwinwindowtabsinput PRIVATE "${header}" "${code}")
endfunction()

add_wayland_protocol(xdg-shell stable/xdg-shell xdg-shell)
add_wayland_protocol(xdg-decoration-unstable-v1 unstable/xdg-decoration xdg-decoration)
add_wayland_protocol(viewporter stable/viewporter viewporter)
add_wayland_protocol(xdg-toplevel-icon-v1 staging/xdg-toplevel-icon xdg-toplevel-icon)

install(TARGETS kwinwindowtabsinput LIBRARY DESTINATION contents/ui/native-input)
install(FILES "${input_module_dir}/qmldir" "${input_module_dir}/kwinwindowtabsinput.qmltypes"
    DESTINATION contents/ui/native-input)
