cmake_minimum_required(VERSION 3.16)
project(Proyecto2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(.)

add_executable(Proyecto2
        scanner.cpp
        scanner.h
        token.cpp
        token.h
        main.cpp
        ast.cpp
        parser.cpp
        visitor.cpp
        Typechecker.cpp)
