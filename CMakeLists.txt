cmake_minimum_required(VERSION 3.20)
project(irixfm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick Qml QuickControls2 Concurrent)
find_package(KF6 REQUIRED COMPONENTS KIO WindowSystem Config CoreAddons Service Solid Archive SyntaxHighlighting)

add_executable(irixfm src/main.cpp src/fm.cpp src/fm.h)
target_link_libraries(irixfm PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Concurrent
    KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::WindowSystem KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::Solid KF6::Archive KF6::SyntaxHighlighting)

include(GNUInstallDirs)
install(TARGETS irixfm)
install(DIRECTORY qml icons DESTINATION ${CMAKE_INSTALL_DATADIR}/irixfm)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/irixfm)
install(FILES irixfm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
