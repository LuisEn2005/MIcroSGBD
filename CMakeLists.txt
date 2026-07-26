cmake_minimum_required(VERSION 3.10)
project(mini-sgbd VERSION 0.1.0 LANGUAGES CXX)

# Configuración de C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Recolectar todos los archivos fuente (.cpp) en src/ recursivamente
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp")

# Crear el ejecutable principal
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Incluir directorios de cabeceras
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Advertencias recomendadas
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# (Opcional) Activar más warnings como errores si se desea
# target_compile_options(${PROJECT_NAME} PRIVATE -Werror)# CMakeLists.txt

