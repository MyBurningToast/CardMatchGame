cmake_minimum_required (VERSION 3.8)

project ("CardMatchGame")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# dependancys
# https://www.glfw.org/download (64-bit Windows binaries)
# https://github.com/g-truc/glm/releases
# https://vulkan.lunarg.com/sdk/home


# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_DIR "C:/libs/glfw-3.5.1.bin.WIN64")
add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${GLFW_DIR}/lib-vc2022/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include"
)

# GLM Header
set(GLM_DIR "C:/libs/glm")

add_executable (CardMatchGame "main.cpp" )

target_include_directories(CardMatchGame PRIVATE ${GLM_DIR})

target_link_libraries(CardMatchGame PRIVATE
    Vulkan::Vulkan
    glfw
    opengl32
    gdi32
    shell32
    user32
)

# Compile shaders
# Vulkan cant read shader source code directly. it needs to be pre compiled bytecode called SPIR-V
# Vulkan SDK has an in built tool for this

# btw i basicly copy pasted this from somones github so dont ask me about the syntax for this lol
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
message(STATUS "GLSLC found at: ${GLSLC}")

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/triangle.vert -o ${CMAKE_BINARY_DIR}/shaders/vert.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/triangle.vert
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/frag.spv
    COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/triangle.frag -o ${CMAKE_BINARY_DIR}/shaders/frag.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/triangle.frag
)

add_custom_target(Shaders DEPENDS ${CMAKE_BINARY_DIR}/shaders/vert.spv ${CMAKE_BINARY_DIR}/shaders/frag.spv)
add_dependencies(CardMatchGame Shaders)