cmake_minimum_required(VERSION 3.21)
project(KWinWindowGroupsInput LANGUAGES C CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS DBus Qml Widgets)
find_package(KWin 6.7.6 EXACT REQUIRED CONFIG)
find_package(KF6I18n REQUIRED)
find_package(KF6Service REQUIRED)
find_package(Wayland REQUIRED Client)
find_package(PkgConfig REQUIRED)
pkg_get_variable(wayland_protocols_data wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

qt_policy(SET QTP0001 NEW)

set(input_module_uri org.kde.kwinwindowgroups.input)
set(input_namespace KWinWindowGroups)
if(DEFINED KWIN_WINDOW_GROUPS_BUILD_ID)
    if(NOT "${KWIN_WINDOW_GROUPS_BUILD_ID}" MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "KWIN_WINDOW_GROUPS_BUILD_ID must be a hexadecimal identifier")
    endif()
    string(APPEND input_module_uri ".rev_${KWIN_WINDOW_GROUPS_BUILD_ID}")
    string(APPEND input_namespace "_${KWIN_WINDOW_GROUPS_BUILD_ID}")
endif()
string(REPLACE "." "/" input_module_path "${input_module_uri}")
set(input_module_dir "${CMAKE_CURRENT_BINARY_DIR}/${input_module_path}")
qt_add_qml_module(kwinwindowgroupsinput
    URI "${input_module_uri}"
    VERSION 1.0
    PLUGIN_TARGET kwinwindowgroupsinput
    OUTPUT_DIRECTORY "${input_module_dir}"
    SOURCES native/moveinput.h native/moveinput.cpp native/groupwindow.h native/groupwindow.cpp
)
target_include_directories(kwinwindowgroupsinput PRIVATE native "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(kwinwindowgroupsinput PRIVATE "KWIN_WINDOW_GROUPS_NAMESPACE=${input_namespace}")
target_link_libraries(kwinwindowgroupsinput PRIVATE KWin::kwin KF6::I18n KF6::Service Qt6::Qml Wayland::Client)
if(DEFINED KWIN_WINDOW_GROUPS_BUILD_ID)
    target_compile_definitions(kwinwindowgroupsinput PRIVATE "KWIN_WINDOW_GROUPS_BUILD_ID=\"${KWIN_WINDOW_GROUPS_BUILD_ID}\"")
    target_link_libraries(kwinwindowgroupsinput PRIVATE Qt6::DBus)
    set_target_properties(kwinwindowgroupsinput PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
endif()

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
    target_sources(kwinwindowgroupsinput PRIVATE "${header}" "${code}")
endfunction()

add_wayland_protocol(xdg-shell stable/xdg-shell xdg-shell)
add_wayland_protocol(xdg-decoration-unstable-v1 unstable/xdg-decoration xdg-decoration)
add_wayland_protocol(viewporter stable/viewporter viewporter)
add_wayland_protocol(xdg-toplevel-icon-v1 staging/xdg-toplevel-icon xdg-toplevel-icon)

install(TARGETS kwinwindowgroupsinput LIBRARY DESTINATION contents/ui/native-input)
install(FILES "${input_module_dir}/qmldir" "${input_module_dir}/kwinwindowgroupsinput.qmltypes"
    DESTINATION contents/ui/native-input)

set(KDE_INSTALL_LOCALEDIR contents/locale)
ki18n_install(po)
