#include "Renderer.h"
#include "Game.h"
#include "Font.h"

#include <vector>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

const int gridWidth = 4;
const int gridHeight = 4;
const float cardSpacing = 0.5f; // world/NDC space distance between card centers
const float cardScale = 0.2f; // half of this is the click hitbox radius
const double revealDuration = 0.8; // seconds both cards stay visible

// TODO read word list from a file
std::vector<WordPair> GetTestWordPairs() {
    return {
        {"A", "A'"},
        {"B", "B'"},
        {"C", "C'"},
        {"D", "D'"},
        {"E", "E'"},
        {"F", "F'"},
        {"G", "G'"},
        {"H", "H'"},
    };
}

// adds one quad per letter to draw text. shrinks it if its to wide
// all positions are in NDC. the fonts glyph sizes are baked pixles so convert with ndcPerPizle
void AddTextQuads(std::vector<QuadInstance>& quads, const Font& font, const std::string& text, float centerX, float centerY, float maxWidth, float maxNdcPerPixel, const glm::vec4& color) {
    std::vector<uint32_t> codepoints = Utf8ToCodepoints(text);

    // total width of the word in baked pixels (sum of each letters advance)
    float textWidth = 0.0f;
    for (uint32_t cp : codepoints) {
        const Glyph* glyph = font.GetGlyph(cp);
        if (!glyph) glyph = font.GetGlyph('?'); // letter missing from the atlas
        if (glyph) textWidth += glyph->advance;
    }
    if (textWidth <= 0.0f) return;

    // use normal size unless it wont fit
    float ndcPerPixel = std::min(maxNdcPerPixel, maxWidth / textWidth);

    // glyphs are positioned relative to the baseline (the line letters sit on)
    // centering on half the height of a capital H means the baseline is the same on every card
    const Glyph* capH = font.GetGlyph('H');
    float halfCapHeight = capH ? -capH->y0 / 2.0f : 0.0f; // y0 is negative (above the baseline)

    float penX = -textWidth / 2.0f; // start at the left edge of the word, so the word is centered

    for (uint32_t cp : codepoints) {
        const Glyph* glyph = font.GetGlyph(cp);
        if (!glyph) glyph = font.GetGlyph('?');
        if (!glyph) continue;

        float glyphWidth = glyph->x1 - glyph->x0;
        float glyphHeight = glyph->y1 - glyph->y0;

        // spaces have no shape. skip drawing but still move the pen along
        if (glyphWidth > 0.0f && glyphHeight > 0.0f) {
            // center of this letter in pixels, relative to the center of the word
            float glyphCenterX = penX + (glyph->x0 + glyph->x1) / 2.0f;
            float glyphCenterY = (glyph->y0 + glyph->y1) / 2.0f + halfCapHeight;

            // the shared quad mesh is 1x1 and centered on the origin, so scale it to the letter size and move it
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(centerX + glyphCenterX * ndcPerPixel, centerY + glyphCenterY * ndcPerPixel, 0.0f));
            model = glm::scale(model, glm::vec3(glyphWidth * ndcPerPixel, glyphHeight * ndcPerPixel, 1.0f));

            quads.push_back({ model, color, glm::vec4(glyph->u0, glyph->v0, glyph->u1, glyph->v1) });
        }

        penX += glyph->advance;
    }
}


int main() {
    Font font;
    // load the font
    if (!font.Load("assets/fonts/Roboto-Regular.ttf", 32.0f)) return -1; // 32 = how many pixels tall the letters are baked
    font.SaveAtlasPNG("font_atlas_debug.png"); // for debugging

    Renderer renderer;
    // the renderer uploads the font atlas to the gpu as its texture
    if (renderer.Init(font.GetAtlasPixels(), font.GetAtlasWidth(), font.GetAtlasHeight()) != 0) return -1;

    Game game;
    std::string gridError;
    if (!game.BuildGrid(GetTestWordPairs(), gridWidth, gridHeight, gridError)) {
        std::cerr << "Failed to build grid: " << gridError << "\n";
        return -1;
    }

    // center of grid
    float originX = -(gridWidth - 1) * cardSpacing / 2.0f;
    float originY = -(gridHeight - 1) * cardSpacing / 2.0f;

    bool mouseWasDown = false;
    double checkPairAt = 0.0;

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
                        checkPairAt = renderer.GetTime() + revealDuration; // start the countdown
                    }
                }
            }
        }

        // while waiting game.HasPendingPair() stays true
        // so that TryFlipCard rejects any new clicks
        if (game.HasPendingPair() && renderer.GetTime() >= checkPairAt) {
            bool matched = game.CheckPair();
            std::cout << (matched ? "Match!\n" : "No match - flipping back\n");

            if (matched && game.IsComplete()) {
                std::cout << "You matched every word!\n";
            }
        }

        // build the quads for this frame. each card is a backround quad, plus its word as letter quads if its face up
        std::vector<QuadInstance> quads;

        glm::vec2 white = font.GetWhitePixelUV();
        glm::vec4 whiteRect(white.x, white.y, white.x, white.y); // a rectangle with no size, so every uv inside it is the white pixel

        for (int y = 0; y < gridHeight; y++) {
            for (int x = 0; x < gridWidth; x++) {
                const CardData& card = game.GetCard(x, y);

                if (card.isMatched) continue; // matched cards disappear TODO: animate it?

                float worldX = originX + x * cardSpacing;
                float worldY = originY + y * cardSpacing;

                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(worldX, worldY, 0.0f));
                model = glm::scale(model, glm::vec3(cardScale, cardScale, 1.0f));

                // face down = blue, face up = orange
                glm::vec4 color = card.isFaceUp ? glm::vec4(1.0f, 0.6f, 0.0f, 1.0f) : glm::vec4(0.2f, 0.4f, 0.9f, 1.0f);

                quads.push_back({ model, color, whiteRect }); // the card background

                // the word goes after the background so its drawn on top of it
                if (card.isFaceUp) {
                    glm::vec4 textColor(0.1f, 0.1f, 0.15f, 1.0f); // near black (contrasts prety good with the orange)
                    // 0.9 leaves a small margin around the card. 0.0025 = 1 atlas pixel per screen pixel at 800px wide
                    // TODO: support differnt window size. Actualy maybe not, getting just this stupid shit to work is absolute torture (i hate trying to render text)
                    AddTextQuads(quads, font, card.word, worldX, worldY, cardScale * 0.9f, 0.0025f, textColor);
                }
            }
        }

        renderer.DrawFrame(quads);
    }

    renderer.WaitIdle();
    renderer.Cleanup();
    return 0;
}