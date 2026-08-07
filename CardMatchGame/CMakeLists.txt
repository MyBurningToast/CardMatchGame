cmake_minimum_required (VERSION 3.8)

project ("CardMatchGame")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_DIR "C:/libs/glfw-3.4.bin.WIN64")
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