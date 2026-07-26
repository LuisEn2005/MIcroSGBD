cmake_minimum_required(VERSION 3.10)

project(
    mini-sgbd
    VERSION 0.1.0
    LANGUAGES CXX
)

# ==========================================
# Configuración general
# ==========================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# ==========================================
# Código principal del Mini-SGBD
# Lista explícita de archivos de código fuente
# ==========================================

set(MINI_SGBD_SOURCES
    # Módulo Storage (Integrante 1)
    src/storage/disk_manager.cpp
    src/storage/page.cpp
    src/storage/slotted_page.cpp

    # Módulo Query Engine (Integrante 3)
    src/query/tokenizer.cpp
    src/query/parser.cpp

    # Añade aquí más fuentes .cpp conforme las vayas creando
)

# Librería principal
add_library(
    mini_sgbd_core
    ${MINI_SGBD_SOURCES}
)

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

# ==========================================
# Advertencias de compilación
# ==========================================

if(MSVC)
    target_compile_options(
        mini_sgbd_core
        PRIVATE
            /W4
    )
else()
    target_compile_options(
        mini_sgbd_core
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

# ==========================================
# Ejecutable principal
# ==========================================

add_executable(
    ${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        mini_sgbd_core
)

# ==========================================
# Pruebas (CTest)
# ==========================================

enable_testing()

# Pruebas de Storage
add_executable(storage_tests tests/storage_tests.cpp)
target_link_libraries(storage_tests PRIVATE mini_sgbd_core)
add_test(NAME StorageTests COMMAND storage_tests)

# Pruebas del Query Engine (Corregido: mini_sgbd_core)
add_executable(query_tests tests/query_tests.cpp)
target_link_libraries(query_tests PRIVATE mini_sgbd_core)
add_test(NAME QueryTests COMMAND query_tests)

if(NOT MSVC)
    target_compile_options(storage_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(query_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME storage_sprint1_tests
    COMMAND storage_tests
)

# ==========================================
# Pruebas del Buffer Pool (Integrante 2)
# ==========================================

add_executable(
    buffer_tests
    tests/buffer_tests.cpp
)

target_link_libraries(
    buffer_tests
    PRIVATE
        mini_sgbd_core
)

if(MSVC)
    target_compile_options(
        buffer_tests
        PRIVATE
            /W4
    )
else()
    target_compile_options(
        buffer_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_test(
    NAME buffer_sprint1_tests
    COMMAND buffer_tests
)
