cmake_minimum_required(VERSION 3.16)
project(dagc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dagger_lib
    src/driver.cpp
    src/lexer.cpp
    src/parser.cpp
    src/runtime.cpp
    src/semantics.cpp
    src/stdlib.cpp
)

target_include_directories(dagger_lib PUBLIC src)
target_compile_definitions(dagger_lib PUBLIC DAGGER_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib")

add_executable(dagc
    src/main.cpp
)

target_link_libraries(dagc PRIVATE dagger_lib)

option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(dagger_cpp_tests
        tests/cpp_tests.cpp
    )
    target_link_libraries(dagger_cpp_tests PRIVATE dagger_lib)
    add_test(NAME dagger_cpp_tests COMMAND dagger_cpp_tests)

    add_executable(dagger_dag_tests
        tests/dag_tests.cpp
    )
    target_link_libraries(dagger_dag_tests PRIVATE dagger_lib)
    target_compile_definitions(dagger_dag_tests PRIVATE DAGGER_TEST_PROGRAMS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/programs")
    add_test(NAME dagger_dag_tests COMMAND dagger_dag_tests)
endif()
