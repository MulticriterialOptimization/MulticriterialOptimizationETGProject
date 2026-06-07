cmake_minimum_required(VERSION 3.23)
project(MulticriterialOptimizationETG)
set(CMAKE_CXX_STANDARD 20)

add_executable(MulticriterialOptimizationETG
    MulticriterialOptimizationETGProject.cpp
    etg.cpp
    etg.h)