#include "Renderer.h"
// #include "Game.h"

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Card {
    glm::vec2 gridPos; // grid coordinates
};

const int gridWidth = 10;
const int gridHeight = 10;
const float cardSpacing = 0.5f; // word space distance between cards

std::vector<Card> cards;

// Builds grid from bottom left. Will later get centered on (0,0)
void BuildCardGrid() {
    cards.clear();
    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            Card card;
            card.gridPos = glm::vec2(x, y);
            cards.push_back(card);
        }
    }
}

int main() {
    Renderer renderer;
    if (renderer.Init() != 0) return -1;

    BuildCardGrid();

    while (!renderer.WindowShouldClose()) {
        renderer.PollEvents();

        // build the list of transforms for this frame - one per card
        // center the grid on (0,0)
        float originX = -(gridWidth - 1) * cardSpacing / 2.0f;
        float originY = -(gridHeight - 1) * cardSpacing / 2.0f;

        std::vector<glm::mat4> transforms;
        for (const auto& card : cards) {
            float worldX = originX + card.gridPos.x * cardSpacing;
            float worldY = originY + card.gridPos.y * cardSpacing;

            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(worldX, worldY, 0.0f));
            model = glm::scale(model, glm::vec3(0.4f, 0.4f, 1.0f)); // 40%

            transforms.push_back(model);
        }

        renderer.DrawFrame(transforms);
    }

    renderer.WaitIdle();
    renderer.Cleanup();
    return 0;
}