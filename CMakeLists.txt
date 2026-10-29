cmake_minimum_required(VERSION 3.25)
project(claudeincus-installer VERSION 3.0.0 DESCRIPTION "Clone Arch into an Incus container with KDE" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(claudeincus-installer
  src/main.cpp
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
target_include_directories(claudeincus-installer PRIVATE src)
target_compile_options(claudeincus-installer PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS claudeincus-installer RUNTIME DESTINATION bin)
