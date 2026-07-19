cmake_minimum_required(VERSION 3.6)

project(usagi-pkgj)

if(NOT HOST_BUILD)
  if(NOT BUILD_VITA)
    message(FATAL_ERROR "Vita toolchain not imported, fix the conan recipe")
  endif()

  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
endif()

# useful for memory alignment constraints
set(CMAKE_CXX_STANDARD 17)

set(VITA_APP_NAME "Usagi PKGj" CACHE STRING "Vita application display name")
set(VITA_TITLEID  "USAG00001" CACHE STRING "Vita Title ID (4 letters + 5 digits)")
set(VITA_VERSION  "0.61" CACHE STRING "Vita application version (XX.XX format for param.sfo)")
set(PKGI_DISPLAY_VERSION "0.61" CACHE STRING "Version string shown in the app header")

option(PKGI_ENABLE_LOGGING "enables debug logging over udp multicast" OFF)

if(PKGI_ENABLE_LOGGING)
  add_definitions(-DPKGI_ENABLE_LOGGING)
endif()

add_definitions(-DPKGI_VERSION="${PKGI_DISPLAY_VERSION}" -D_GNU_SOURCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fvisibility=hidden")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fvisibility=hidden")

find_package(fmt REQUIRED)
find_package(Boost REQUIRED)
find_package(cereal REQUIRED)
find_package(libzip REQUIRED)

if(HOST_BUILD)
  include(host.cmake)
else()
  include(cross.cmake)
endif()
