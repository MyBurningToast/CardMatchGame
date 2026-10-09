#include "Renderer.h"
#include "Game.h"

#include <vector>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

const int gridWidth = 4;
const int gridHeight = 4;
const float cardSpacing = 0.5f; // world/NDC space distance between card centers
const float cardScale = 0.2f; // half of this is the click hitbox radius

// TODO read word list from a file
std::vector<WordPair> GetTestWordPairs() {
    return {
        {"a1", "a2"},
        {"b1", "b2"},
        {"c1", "c2"},
        {"d1", "d2"},
        {"e1", "e2"},
        {"f1", "f2"},
        {"g1", "g1"},
        {"h1", "h1"},
    };
}

int main() {
    Renderer renderer;
    if (renderer.Init() != 0) return -1;

    Game game;
    if (!game.BuildGrid(GetTestWordPairs(), gridWidth, gridHeight)) {
        std::cerr << "Failed to build grid\n"; // TODO add more infomation for user. COuld have failed beacuse of an incorrect word list
        return -1;
    }

    // center of grid
    float originX = -(gridWidth - 1) * cardSpacing / 2.0f;
    float originY = -(gridHeight - 1) * cardSpacing / 2.0f;

    bool mouseWasDown = false;

    while (!renderer.WindowShouldClose()) {
        renderer.PollEvents(); // glfw gets user input

        // handel click input
        bool mouseIsDown = renderer.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
        bool clickedThisFrame = mouseIsDown && !mouseWasDown;
        mouseWasDown = mouseIsDown;

        if (clickedThisFrame) {
            double mouseX, mouseY;
            renderer.GetCursorPos(mouseX, mouseY); // pixle coords

            int fbWidth, fbHeight;
            renderer.GetFramebufferSize(fbWidth, fbHeight);

            // convert pixel coords to the same space the quads are positioned in
            // vulkan NDC is -1 to 1 with Y pointing donw. Same direction as screen pixles (no dont need a y flip)
            float ndcX = (2.0f * (float)mouseX) / (float)fbWidth - 1.0f;
            float ndcY = (2.0f * (float)mouseY) / (float)fbHeight - 1.0f;

            // calculate out what grid cell is closest to the click
            int gridX = (int)std::round((ndcX - originX) / cardSpacing);
            int gridY = (int)std::round((ndcY - originY) / cardSpacing);

            if (gridX >= 0 && gridX < gridWidth && gridY >= 0 && gridY < gridHeight) {
                // check the clikc is within the cards visible bounds
                float cardCenterX = originX + gridX * cardSpacing;
                float cardCenterY = originY + gridY * cardSpacing;

                // Checks if the point is within half the cards width and height from its center
                if (std::abs(ndcX - cardCenterX) <= cardScale / 2.0f && std::abs(ndcY - cardCenterY) <= cardScale / 2.0f) {

                    FlipResult result = game.TryFlipCard(gridX, gridY);

                    if (result == FlipResult::Flipped) {
                        std::cout << "Flipped (" << gridX << "," << gridY << "): " << game.GetCard(gridX, gridY).word << "\n";
                    }
                    else if (result == FlipResult::FlippedSecond) {
                        std::cout << "Flipped second (" << gridX << "," << gridY << "): " << game.GetCard(gridX, gridY).word << "\n";
                    }
                }
            }
        }

        // resolve pending flips
        // TODO: add delay/animation here
        if (game.IsResolving()) {
            bool matched = game.ResolvePendingFlips();
            std::cout << (matched ? "Match!\n" : "No match - flipping back\n");

            if (matched && game.IsComplete()) {
                std::cout << "You matched every word!\n";
            }
        }

        // build transforms for this frame based on current card state
        std::vector<glm::mat4> transforms;
        for (int y = 0; y < gridHeight; y++) {
            for (int x = 0; x < gridWidth; x++) {
                const CardData& card = game.GetCard(x, y);

                if (card.isMatched) continue; // matched cards disappear for now // TODO: keep them face up after i add actual text one them

                float worldX = originX + x * cardSpacing;
                float worldY = originY + y * cardSpacing;

                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(worldX, worldY, 0.0f));

                // TODO add visuals
                // temporary until textures exist
                // face up is full size, face down is smaller
                float scale = card.isFaceUp ? cardScale : cardScale * 0.6f;
                model = glm::scale(model, glm::vec3(scale, scale, 1.0f));

                transforms.push_back(model);
            }
        }

        renderer.DrawFrame(transforms);
    }

    renderer.WaitIdle();
    renderer.Cleanup();
    return 0;
}