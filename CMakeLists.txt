cmake_minimum_required(VERSION 3.23)
project(MulticriterialOptimizationETG)
set(CMAKE_CXX_STANDARD 20)

add_library(etg_lib STATIC
    src/etg.cpp
    src/etg_prep.cpp
    src/genes.cpp
    src/spanning_tree.cpp
    src/evaluator.cpp
    src/ga.cpp)
target_include_directories(etg_lib PUBLIC src/)

add_executable(etg_solver src/main.cpp)
target_link_libraries(etg_solver PRIVATE etg_lib)
set_target_properties(etg_solver PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_target_properties(etg_solver PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "input.txt --tmax 100"
)
set(INPUT_FILE "input.txt" CACHE STRING "Input file for ETG")
target_compile_definitions(etg_solver PRIVATE
    INPUT_FILE="${INPUT_FILE}"
)
configure_file(${CMAKE_SOURCE_DIR}/input.txt
               ${CMAKE_BINARY_DIR}/Release/input.txt
               COPYONLY)