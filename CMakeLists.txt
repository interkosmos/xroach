cmake_minimum_required(VERSION 3.5)
project(xroach)

set(CMAKE_C_STANDARD 99)
set(SOURCE_FILES xroach.c)

add_executable(xroach ${SOURCE_FILES})

find_package(X11 REQUIRED)

include_directories(${X11_INCLUDE_DIR})
target_link_libraries(xroach m ${X11_LIBRARIES})
