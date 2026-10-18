## Setup (Windows only)

**Dependencies**
1. Visual Studio 2022 with "Desktop development with C++" workload
2. CMake 3.8+ (Already comes with Visual Studio)
3. [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
4. [GLFW (64-bit Windows binaries)](https://www.glfw.org/download)
5. [GLM](https://github.com/g-truc/glm)

**Setup**
- Install Vulkan SDK (installer sets `VULKAN_SDK` env variable automatically)
- Unzip GLFW to `C:\libs\glfw-3.5.1.bin.WIN64`
- Unzip GLM to `C:\libs\glm`
- If using different folders, change the `GLFW_DIR` and `GLM_DIR` paths in `CMakeLists.txt`

## Controls
- Left click: flip a card
- R: restart with a new shuffle
- Escape: quit

## Current Progress
Very minimal game loop. Cards render their text from the word list in `assets/words.txt`.
