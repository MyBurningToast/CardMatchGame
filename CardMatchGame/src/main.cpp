#include "Renderer.h"
#include "Game.h"
#include "Font.h"
#include "WordList.h"

#include <vector>
#include <iostream>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

constexpr int gridWidth = 4;
constexpr int gridHeight = 4;
constexpr float cardSpacing = 0.5f; // world/NDC space distance between card centers
constexpr float cardScale = 0.2f; // half of this is the click hitbox radius
constexpr double revealDuration = 0.8; // seconds both cards stay visible
const std::string wordListPath = "assets/words.txt"; // one pair per line: maori,english

// win screen (shown on the empty board once every pair is matched)
constexpr glm::vec4 winTextColor(1.0f, 1.0f, 1.0f, 1.0f); // white
constexpr float winTitleScale = 0.005f; // NDC per atlas pixel for the "You win!" line. double the normal text size
constexpr float winDetailScale = 0.0035f; // NDC per atlas pixel for the smaller lines under it
constexpr float winLineSpacing = 0.25f; // NDC distance between the lines
constexpr float winTextMaxWidth = 1.6f; // NDC width a line is allowed to take up before it shrinks
const std::string winTitleText = "You win!";
const std::string movesLabel = "Moves: ";
const std::string restartPromptText = "Press R to restart";
const std::string lineSeparator = "==============================\n\n";
constexpr float winTextCenterX = 0.0f;
constexpr float winTitleY = -winLineSpacing;
constexpr float winMovesY = 0.0f;
constexpr float winPromptY = winLineSpacing;

// font
const std::string fontPath = "assets/fonts/Roboto-Regular.ttf";
constexpr float fontPixelHeight = 32.0f; // how many pixels tall the letters are baked
const std::string debugAtlasPath = "font_atlas_debug.png";
constexpr char capHeightReferenceChar = 'H'; // used to find a shared text baseline

// cards
constexpr float ndcSpan = 2.0f; // NDC is from -1 to 1, so it is 2 units wide
constexpr float ndcMin = -1.0f; // the left and top edge of NDC space
constexpr float half = 0.5f; // fun fact: multiplication takes less cpu cycles than devision (even if its a fractional number)
constexpr int pairCount = gridWidth * gridHeight / 2;
constexpr float cardHitRadius = cardScale * half; // how far from a card's center a click still hits it
constexpr glm::vec4 cardFaceUpColor(1.0f, 0.6f, 0.0f, 1.0f); // orange
constexpr glm::vec4 cardFaceDownColor(0.2f, 0.4f, 0.9f, 1.0f); // blue
constexpr glm::vec4 cardTextColor(0.1f, 0.1f, 0.15f, 1.0f); // near black (contrasts pretty good with the orange)
constexpr float cardTextWidthFraction = 0.9f; // leaves a small margin around the card
constexpr float cardTextScale = 0.0025f; // 1 atlas pixel per screen pixel at 800px wide

constexpr double noCheckScheduled = 0.0; // checkPairAt value while no pair is waiting

// adds one quad per letter to draw text. shrinks it if it's too wide
// all positions are in NDC. the font's glyph sizes are baked pixels so convert with ndcPerPixel
void AddTextQuads(std::vector<QuadInstance>& quads, const Font& font, const std::string& text, float centerX, float centerY, float maxWidth, float maxNdcPerPixel, const glm::vec4& color) {
    std::vector<uint32_t> codepoints = Utf8ToCodepoints(text);

    // total width of the word in baked pixels (sum of each letters advance)
    float textWidth = 0.0f;
    for (uint32_t cp : codepoints) {
        const Glyph* glyph = font.GetGlyph(cp); // GetGlyph falls back to '?' for letters the font doesn't have
        if (glyph) textWidth += glyph->advance;
    }
    if (textWidth <= 0.0f) return;

    // use normal size unless it won't fit
    float ndcPerPixel = std::min(maxNdcPerPixel, maxWidth / textWidth);

    // glyphs are positioned relative to the baseline (the line letters sit on)
    // centering on half the height of a capital H means the baseline is the same on every card
    const Glyph* capH = font.GetGlyph(capHeightReferenceChar);
    float halfCapHeight = capH ? -capH->y0 * half : 0.0f; // y0 is negative (above the baseline)

    float penX = -textWidth * half; // start at the left edge of the word, so the word is centered

    for (uint32_t cp : codepoints) {
        const Glyph* glyph = font.GetGlyph(cp);
        if (!glyph) continue;

        float glyphWidth = glyph->x1 - glyph->x0;
        float glyphHeight = glyph->y1 - glyph->y0;

        // spaces have no shape. skip drawing but still move the pen along
        if (glyphWidth > 0.0f && glyphHeight > 0.0f) {
            // center of this letter in pixels, relative to the center of the word
            float glyphCenterX = penX + (glyph->x0 + glyph->x1) * half;
            float glyphCenterY = (glyph->y0 + glyph->y1) * half + halfCapHeight;

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
    if (!font.Load(fontPath, fontPixelHeight)) return EXIT_FAILURE;
#ifndef NDEBUG
    font.SaveAtlasPNG(debugAtlasPath);
#endif

    // the word list is checked against the letters the font can draw, so the font has to load first
    std::vector<WordPair> wordPairs;
    std::string wordError;
    if (!LoadWordPairs(wordListPath, font, (size_t)pairCount, wordPairs, wordError)) {
        std::cerr << "Failed to load word list: " << wordError << "\n";
        return EXIT_FAILURE;
    }

    Renderer renderer;
    // the renderer uploads the font atlas to the gpu as its texture
    if (renderer.Init(font.GetAtlasPixels(), font.GetAtlasWidth(), font.GetAtlasHeight()) != renderOk) return EXIT_FAILURE;

    Game game;
    std::string gridError;
    if (!game.BuildGrid(wordPairs, gridWidth, gridHeight, gridError)) {
        std::cerr << "Failed to build grid: " << gridError << "\n";
        return EXIT_FAILURE;
    }

    // center of grid
    float originX = -(gridWidth - 1) * cardSpacing * half;
    float originY = -(gridHeight - 1) * cardSpacing * half;

    bool mouseWasDown = false;
    bool restartWasDown = false;
    double checkPairAt = noCheckScheduled;

    std::cout << lineSeparator;
    while (!renderer.WindowShouldClose()) {
        renderer.PollEvents(); // glfw gets user input

        // escape quits. the loop ends and Cleanup runs as normal
        if (renderer.IsKeyPressed(GLFW_KEY_ESCAPE)) {
            renderer.RequestClose();
        }

        // R restarts. only react to the frame the key goes down, not while it is held
        bool restartIsDown = renderer.IsKeyPressed(GLFW_KEY_R);
        if (restartIsDown && !restartWasDown) {
            // BuildGrid reshuffles the cards and clears any face up or pending cards
            if (!game.BuildGrid(wordPairs, gridWidth, gridHeight, gridError)) {
                std::cerr << "Failed to restart: " << gridError << "\n";
                break;
            }
            std::cout << "Restarted\n\n";
            std::cout << lineSeparator;
        }
        restartWasDown = restartIsDown;

        // handle click input
        bool mouseIsDown = renderer.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
        bool clickedThisFrame = mouseIsDown && !mouseWasDown;
        mouseWasDown = mouseIsDown;

        if (clickedThisFrame) {
            double mouseX, mouseY;
            renderer.GetCursorPos(mouseX, mouseY); // pixel coords

            int fbWidth, fbHeight;
            renderer.GetFramebufferSize(fbWidth, fbHeight);

            // convert pixel coords to the same space the quads are positioned in
            // vulkan NDC is -1 to 1 with Y pointing down. Same direction as screen pixels (no don't need a y flip)
            float ndcX = (ndcSpan * (float)mouseX) / (float)fbWidth + ndcMin;
            float ndcY = (ndcSpan * (float)mouseY) / (float)fbHeight + ndcMin;

            // calculate out what grid cell is closest to the click
            int gridX = (int)std::round((ndcX - originX) / cardSpacing);
            int gridY = (int)std::round((ndcY - originY) / cardSpacing);

            if (gridX >= 0 && gridX < gridWidth && gridY >= 0 && gridY < gridHeight) {
                // check the click is within the card's visible bounds
                float cardCenterX = originX + gridX * cardSpacing;
                float cardCenterY = originY + gridY * cardSpacing;

                // Checks if the point is within half the card's width and height from its center
                if (std::abs(ndcX - cardCenterX) <= cardHitRadius && std::abs(ndcY - cardCenterY) <= cardHitRadius) {

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
            std::cout << (matched ? "Match!\n\n" : "No match - flipping back\n\n");

            if (matched && game.IsComplete()) {
                std::cout << lineSeparator;
                std::cout << "You matched every word! Press R to play again or Escape to quit.\n";
            }
        }

        // build the quads for this frame. each card is a background quad, plus its word as letter quads if it's face up
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
                glm::vec4 color = card.isFaceUp ? cardFaceUpColor : cardFaceDownColor;

                quads.push_back({ model, color, whiteRect }); // the card background

                // the word goes after the background so it's drawn on top of it
                if (card.isFaceUp) {
                    AddTextQuads(quads, font, card.word, worldX, worldY, cardScale * cardTextWidthFraction, cardTextScale, cardTextColor);
                }
            }
        }

        // every card is matched so the board is empty. show the win screen
        if (game.IsComplete()) {
            AddTextQuads(quads, font, winTitleText, winTextCenterX, winTitleY, winTextMaxWidth, winTitleScale, winTextColor);
            AddTextQuads(quads, font, movesLabel + std::to_string(game.GetMoves()), winTextCenterX, winMovesY, winTextMaxWidth, winDetailScale, winTextColor);
            AddTextQuads(quads, font, restartPromptText, winTextCenterX, winPromptY, winTextMaxWidth, winDetailScale, winTextColor);
        }

        renderer.DrawFrame(quads);
    }

    renderer.WaitIdle();
    renderer.Cleanup();
    return EXIT_SUCCESS;
}