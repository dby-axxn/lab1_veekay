function(add_shaders TARGET_NAME)
    set(SHADER_SOURCE_FILES ${ARGN})

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

    set(SHADER_PRODUCTS)

    foreach (SHADER_SOURCE IN LISTS SHADER_SOURCE_FILES)
        cmake_path(ABSOLUTE_PATH SHADER_SOURCE NORMALIZE)
        cmake_path(GET SHADER_SOURCE FILENAME SHADER_NAME)

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

        list(APPEND SHADER_PRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
    endforeach ()

    add_custom_target(${TARGET_NAME} ALL
            DEPENDS ${SHADER_PRODUCTS}
            COMMENT "Building shader target: ${TARGET_NAME}"
    )
endfunction()

add_shaders(shaders_target
        ${CMAKE_SOURCE_DIR}/shaders/shader.vert
        ${CMAKE_SOURCE_DIR}/shaders/shader.frag
)

add_dependencies(${PROJECT_NAME} shaders_target)