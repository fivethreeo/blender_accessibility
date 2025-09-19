cmake_minimum_required(VERSION 3.16)
project(ControlPanel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(SOURCES main.cpp)

add_executable(ControlPanel ${SOURCES})

target_link_libraries(ControlPanel PRIVATE Qt6::Core Qt6::Widgets)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(ControlPanel PRIVATE X11)
    target_include_directories(ControlPanel PRIVATE ${X11_INCLUDE_DIR})
endif()
