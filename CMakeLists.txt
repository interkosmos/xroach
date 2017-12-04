cmake_minimum_required(VERSION 3.9)
project(xroach)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES xroach.c)

add_executable(xroach ${SOURCE_FILES})

find_package(X11 REQUIRED)

include_directories(${X11_INCLUDE_DIR})
target_link_libraries(xroach ${X11_LIBRARIES} m)