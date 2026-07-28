cmake_minimum_required(VERSION 3.10)

project(
    mini-sgbd
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(MINI_SGBD_SOURCES
    src/storage/disk_manager.cpp
    src/storage/page.cpp
    src/storage/slotted_page.cpp
    src/storage/heap_file.cpp

    src/buffer/clock_replacer.cpp
    src/buffer/buffer_pool_manager.cpp
    
    src/catalog/catalog_manager.cpp

    src/query/tokenizer.cpp
    src/query/parser.cpp
    src/query/executor.cpp
)

add_library(mini_sgbd_core ${MINI_SGBD_SOURCES})

target_include_directories(
    mini_sgbd_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    mini_sgbd_core
    PUBLIC
        Threads::Threads
)

function(enable_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()

enable_project_warnings(mini_sgbd_core)

add_executable(mini-sgbd src/main.cpp)
target_link_libraries(mini-sgbd PRIVATE mini_sgbd_core)
enable_project_warnings(mini-sgbd)

enable_testing()

add_executable(storage_tests tests/storage_tests.cpp)
target_link_libraries(storage_tests PRIVATE mini_sgbd_core)
enable_project_warnings(storage_tests)
add_test(NAME storage_tests COMMAND storage_tests)

add_executable(heap_file_tests tests/heap_file_tests.cpp)
target_link_libraries(heap_file_tests PRIVATE mini_sgbd_core)
enable_project_warnings(heap_file_tests)
add_test(NAME heap_file_tests COMMAND heap_file_tests)

add_executable(buffer_tests tests/buffer_tests.cpp)
target_link_libraries(buffer_tests PRIVATE mini_sgbd_core)
enable_project_warnings(buffer_tests)
add_test(NAME buffer_tests COMMAND buffer_tests)

add_executable(query_tests tests/query_tests.cpp)
target_link_libraries(query_tests PRIVATE mini_sgbd_core)
enable_project_warnings(query_tests)
add_test(NAME query_tests COMMAND query_tests)

# Pruebas de Integracion (Fase 3)
add_executable(integration_tests tests/integration_tests.cpp)
target_link_libraries(integration_tests PRIVATE mini_sgbd_core)
enable_project_warnings(integration_tests)
add_test(NAME integration_tests COMMAND integration_tests)

set_tests_properties(
    storage_tests
    heap_file_tests
    buffer_tests
    query_tests
    integration_tests
    PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
