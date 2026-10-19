cmake_minimum_required(VERSION 3.21)

project("CardMatchGame")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TODO: automatically fetch and compile the required headers for all dependencies

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_DIR "C:/libs/glfw-3.5.1.bin.WIN64" CACHE PATH "GLFW directory") # change this to your correct path
add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${GLFW_DIR}/lib-vc2022/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include"
)

# GLM (header only)
set(GLM_DIR "C:/libs/glm" CACHE PATH "GLM directory") # change this to your correct path

add_executable(CardMatchGame
    "src/main.cpp"
    "src/Renderer.cpp" "src/Renderer.h"
    "src/Game.cpp" "src/Game.h"
    "src/Font.cpp" "src/Font.h"
    "src/WordList.cpp"
    "third_party/stb_image_write.h"
)

# SYSTEM keeps warnings from third party headers out of the build
target_include_directories(CardMatchGame SYSTEM PRIVATE
    ${GLM_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(CardMatchGame PRIVATE
    Vulkan::Vulkan
    glfw
    gdi32
    shell32
    user32
)

# copy assets (fonts etc) next to the exe so the program can find them with a relative path
add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
)
add_dependencies(CardMatchGame CopyAssets)

if(MSVC) # gcc and clang already default to utf-8
    target_compile_options(CardMatchGame PRIVATE
        /utf-8 # read source files as utf-8 (because of the macrons)
        /W4
    )
endif()

if(MSVC)
    target_link_options(CardMatchGame PRIVATE
        "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>" # Prevents Windows from creating a console window
        "$<$<CONFIG:Release>:/ENTRY:mainCRTStartup>" # Tells the linker to use the C++ runtime startup function that calls main()
    )
endif()

# compile shaders
# vulkan can't read shader source directly, it needs precompiled bytecode called SPIR-V
# the vulkan sdk has a built in tool for this
find_program(GLSLC glslc REQUIRED HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

# done at configure time so parallel builds can't race on the directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/vert.spv
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