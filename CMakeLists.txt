cmake_minimum_required(VERSION 3.25)
set(CCCU_RELEASE_DATE "29-10-2026")
project(cccu VERSION 1.0 DESCRIPTION "Claudemods Container Creator Unity" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(cccu
  src/main.cpp
  src/menu.cpp
  src/util.cpp
  src/proc.cpp
  src/config.cpp
  src/distro.cpp
  src/drive.cpp
  src/hostinstall.cpp
  src/incus.cpp
  src/profiles.cpp
  src/sources.cpp
  src/rootfs.cpp
  src/installer.cpp
)
target_include_directories(cccu PRIVATE src)
target_compile_definitions(cccu PRIVATE CCCU_VERSION="${PROJECT_VERSION}" CCCU_DATE="${CCCU_RELEASE_DATE}")
target_compile_options(cccu PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS cccu RUNTIME DESTINATION bin)

# Qt6 desktop version (cccu-gui): a front-end that runs the cccu command line tool.
#   cmake -S . -B build -DCCCU_BUILD_GUI=ON && cmake --build build -j
option(CCCU_BUILD_GUI "Build the Qt6 GUI (cccu-gui)" OFF)
if(CCCU_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
  add_executable(cccu-gui gui/main.cpp gui/mainwindow.cpp gui/mainwindow.hpp)
  target_compile_definitions(cccu-gui PRIVATE CCCU_GUI_VERSION="${PROJECT_VERSION}" CCCU_DATE="${CCCU_RELEASE_DATE}")
  target_link_libraries(cccu-gui PRIVATE Qt6::Widgets)
  add_dependencies(cccu-gui cccu)   # the GUI needs the backend next to it
  install(TARGETS cccu-gui RUNTIME DESTINATION bin)

  # Application-menu entry and icon, so the GUI is started by clicking (it asks for the password itself).
  include(GNUInstallDirs)
  configure_file(packaging/cccu.desktop.in ${CMAKE_BINARY_DIR}/cccu.desktop @ONLY)
  install(FILES ${CMAKE_BINARY_DIR}/cccu.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/cccu.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
