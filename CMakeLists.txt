cmake_minimum_required(VERSION 3.21)

project(CollaboWrite VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(CollaboWrite
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(CollaboWrite PRIVATE Qt6::Widgets)
