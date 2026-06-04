cmake_minimum_required(VERSION 4.0)
project(Proyecto2)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(Proyecto2
        scanner.cpp
        scanner.h
        token.cpp
        token.h
        main.cpp
        ast.cpp
        parser.cpp)
