# Минимальная версия CMake
cmake_minimum_required(VERSION 3.20)

# Включаем модуль для загрузки внешних зависимостей
include(FetchContent)

# Создаем проект с поддержкой C и C++
project(testbed LANGUAGES C CXX)

# Создаем исполняемый файл
add_executable(${PROJECT_NAME} main.cpp)

# Устанавливаем стандарт C++20 для проекта
set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD_REQUIRED TRUE
        CXX_STANDARD 20)

# Ищем Vulkan SDK
find_package(Vulkan REQUIRED)

# Связываем библиотеку veekay и заголовочные файлы Vulkan
target_link_libraries(${PROJECT_NAME} veekay Vulkan::Headers)

# Функция для компиляции шейдеров
function(add_shaders TARGET_NAME)
    # Получаем список файлов шейдеров из аргументов функции
    set(SHADER_SOURCE_FILES ${ARGN})

    # Создаем директорию для скомпилированных шейдеров
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

    # Список выходных файлов (.spv)
    set(SHADER_PRODUCTS)

    foreach(SHADER_SOURCE IN LISTS SHADER_SOURCE_FILES)
        # Получаем абсолютный путь и имя файла
        cmake_path(ABSOLUTE_PATH SHADER_SOURCE NORMALIZE)
        cmake_path(GET SHADER_SOURCE FILENAME SHADER_NAME)

        # Команда для компиляции шейдера
        add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv
                COMMAND Vulkan::glslc
                ${SHADER_SOURCE}
                -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv
                DEPENDS ${SHADER_SOURCE}
                COMMENT "Compiling shader: ${SHADER_NAME}"
                VERBATIM
        )

        # Добавляем скомпилированный файл в список продуктов
        list(APPEND SHADER_PRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
    endforeach()

    # Создаем целевую зависимость для шейдеров
    add_custom_target(${TARGET_NAME} ALL
            DEPENDS ${SHADER_PRODUCTS}
            COMMENT "Building shader target: ${TARGET_NAME}"
    )
endfunction()

# Компилируем шейдеры
add_shaders(shaders_target
        ${CMAKE_SOURCE_DIR}/shaders/shader.vert
        ${CMAKE_SOURCE_DIR}/shaders/shader.frag
)

# Добавляем зависимость от целевой сборки шейдеров
add_dependencies(${PROJECT_NAME} shaders_target)