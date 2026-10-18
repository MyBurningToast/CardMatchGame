cmake_minimum_required (VERSION 3.8)

project ("CardMatchGame")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TODO: Automaticly fetch and compile the required headers for all dependacys

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_DIR "C:/libs/glfw-3.5.1.bin.WIN64") # CHANGE THIS TO YOUR CORRECT PATH
add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${GLFW_DIR}/lib-vc2022/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include"
)

# GLM Header
set(GLM_DIR "C:/libs/glm") # CHANGE THIS TO YOUR CORRECT PATH

add_executable (CardMatchGame "src/main.cpp" "src/Renderer.cpp" "src/Renderer.h" "src/Game.cpp" "src/Game.h" "src/Font.cpp" "src/Font.h" "third_party/stb_image_write.h" "src/WordList.cpp")

target_include_directories(CardMatchGame PRIVATE ${GLM_DIR} ${CMAKE_SOURCE_DIR}/third_party)

target_link_libraries(CardMatchGame PRIVATE
    Vulkan::Vulkan
    glfw
    opengl32
    gdi32
    shell32
    user32
)

# Copy assets (fonts etc) next to the exe so the program can find them with a relative path
add_custom_command(TARGET CardMatchGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
)

if(MSVC) # GCC and Clang compilers already default to UTF-8
    target_compile_options(CardMatchGame PRIVATE /utf-8) # Read source files as UTF-8 (beacuse of the macrons)
endif()

# Compile shaders
# Vulkan cant read shader source code directly. it needs to be pre compiled bytecode called SPIR-V
# Vulkan SDK has an in built tool for this
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

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