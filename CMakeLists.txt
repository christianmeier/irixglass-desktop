cmake_minimum_required(VERSION 3.20)
project(irixfm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

# WebEngineQuick isn't linked (see main.cpp), only needed at run time for web tabs.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick Qml QuickControls2 Concurrent Network DBus)
find_package(KF6 REQUIRED COMPONENTS KIO WindowSystem Config CoreAddons Service Solid Archive SyntaxHighlighting Pty)
find_library(VTERM_LIB vterm REQUIRED)

add_executable(irixfm src/main.cpp src/fm.cpp src/fm.h src/terminal.cpp src/terminal.h src/filemanager1.h)
target_link_libraries(irixfm PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Concurrent Qt6::Network Qt6::DBus
    KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::WindowSystem KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::Solid KF6::Archive KF6::SyntaxHighlighting KF6::Pty ${VTERM_LIB})

include(GNUInstallDirs)
install(TARGETS irixfm)
install(DIRECTORY qml icons DESTINATION ${CMAKE_INSTALL_DATADIR}/irixfm)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/irixfm)
install(FILES irixfm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# "Show in folder" in other apps: D-Bus activation of fm for org.freedesktop.FileManager1
file(READ org.freedesktop.FileManager1.service FM1_SERVICE)
string(REPLACE "Exec=irixfm" "Exec=${CMAKE_INSTALL_FULL_BINDIR}/irixfm" FM1_SERVICE "${FM1_SERVICE}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.FileManager1.service "${FM1_SERVICE}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.FileManager1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
