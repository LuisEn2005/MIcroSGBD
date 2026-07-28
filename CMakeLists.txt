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
    src/storage/record_codec.cpp
    src/storage/table_storage.cpp

    src/index/hash_index_header_page.cpp
    src/index/bucket_page.cpp
    src/index/index_key.cpp
    src/index/hash_index.cpp

    src/buffer/clock_replacer.cpp
    src/buffer/buffer_pool_manager.cpp

    src/catalog/catalog_manager.cpp

    src/query/tokenizer.cpp
    src/query/parser.cpp
    src/query/executor.cpp
    src/query/operators/seq_scan_operator.cpp
    src/query/operators/filter_operator.cpp
    src/query/operators/projection_operator.cpp
    src/query/operators/index_scan_operator.cpp
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

function(add_minidb_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE mini_sgbd_core)
    enable_project_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(
        ${target}
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endfunction()

add_minidb_test(storage_tests tests/storage_tests.cpp)
add_minidb_test(heap_file_tests tests/heap_file_tests.cpp)
add_minidb_test(index_tests tests/index_tests.cpp)
add_minidb_test(buffer_tests tests/buffer_tests.cpp)
add_minidb_test(query_tests tests/query_tests.cpp)
add_minidb_test(integration_tests tests/integration_tests.cpp)
add_minidb_test(sprint3_acceptance_test tests/sprint3_acceptance_test.cpp)

add_minidb_test(sprint4_storage_tests tests/sprint4_storage_tests.cpp)
add_minidb_test(sprint4_metrics_tests tests/sprint4_metrics_tests.cpp)
