cmake_minimum_required(VERSION 3.23)
project(MulticriterialOptimizationETG)
set(CMAKE_CXX_STANDARD 20)

add_library(etg_lib STATIC
    src/etg.cpp
    src/etg_prep.cpp
    src/genes.cpp
    src/spanning_tree.cpp
    src/evaluator.cpp
    src/ga.cpp
    src/gp_tree.cpp
    src/gp_ops.cpp)
target_include_directories(etg_lib PUBLIC src/)

add_executable(etg_solver src/main.cpp)
target_link_libraries(etg_solver PRIVATE etg_lib)

enable_testing()
foreach(test_name test_etg_prep test_gp_tree test_gp_ops)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE etg_lib)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
