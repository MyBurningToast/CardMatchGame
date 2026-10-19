# CardMatchGame (School project)

A little memory game I made in C++20 using Vulkan. The goal is to match Māori words with their English translations.

## Download

Get the latest build from [Releases](../../releases).

## How to Play

- Click two cards to flip them over.
- Match a Māori word with its English translation.
- Wrong pairs flip back after a short delay.
- Match every pair to win!

### Controls

- Left click to flip cards
- `R` to restart
- `Esc` to quit

## Features

- 4x4 grid of cards
- Te reo Māori words, including macrons
- Shuffled cards each game
- Move counter and win screen
- Words loaded from a text file

## Building from Source
### Requirements

- Windows 10/11
- Visual Studio 2022/2026 with the C++ workload
- CMake 3.21+
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
- [GLFW 3.5.1](https://www.glfw.org/download)
- [GLM](https://github.com/g-truc/glm)

### Building

1. Install the requirements.
2. Extract GLFW to `C:\libs\glfw-3.5.1.bin.WIN64` and GLM to `C:\libs\glm`, or update `GLFW_DIR` and `GLM_DIR` in `CMakeLists.txt`.
3. Open the `CardMatchGame` folder in Visual Studio.
4. Select the x64 Debug or Release preset and build. (you may need to set `CMakeLists.txt` as startup item in visual studio)
5. Run `CardMatchGame.exe` from the build folder.

## Word List

The words are stored in `assets/words.txt`, with one pair per line:

```text
kurī,dog
manu,bird
whānau,family
```

The default grid needs 8 pairs. The game checks the file for errors when it loads.

## Project Structure

- `main.cpp` – Main game loop and input
- `Game.cpp` – Game logic
- `WordList.cpp` – Loads the words
- `Font.cpp` – Handles fonts and text
- `Renderer.cpp` – Vulkan rendering
- `assets/` – Words and fonts
- `shaders/` – Graphics shaders

## Credits

- [Roboto](https://github.com/googlefonts/roboto-classic) – Font
- [stb](https://github.com/nothings/stb) – Utility libraries
- [GLFW](https://www.glfw.org/), [GLM](https://github.com/g-truc/glm) and the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
