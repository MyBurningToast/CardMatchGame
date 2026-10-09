#pragma once
#include <vector>
#include <string>

enum class CardLanguage {
    Maori,
    English
};

struct CardData {
    int pairId; // used for matching cards
    std::string word; // text shown on card
    CardLanguage language;
    bool isFaceUp = false;
    bool isMatched = false;
};

struct WordPair {
    std::string maori;
    std::string english;
};

enum class FlipResult {
    Flipped, // first card of a turn flipped
    FlippedSecond, // 2nd card fliped. caller should schedule ResolvePendingFlips
    RejectedAlreadyFaceUp,
    RejectedMatched,
    RejectedBusy, // a pair waiting to be resolved
    RejectedOutOfBounds
};

class Game {
public:
    // builds shuffled grid
    bool BuildGrid(const std::vector<WordPair>& wordPairs, int width, int height);
    
    // read only access for rendering. returns const reference
    const CardData& GetCard(int x, int y) const;

    FlipResult TryFlipCard(int x, int y);
    bool ResolvePendingFlips(); // returns true if it was a match
    bool IsComplete() const;

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    bool IsResolving() const { return isResolving; } // TODO: let the UI know when to start the resolve delay

private:
    int width = 0;
    int height = 0;
    std::vector<CardData> grid; // flattened 2D grid. index = y * width + x

    int firstX = -1, firstY = -1;
    int secondX = -1, secondY = -1;
    bool isResolving = false;

    // private because only Game should be able to modify cards directly
    CardData& CardAt(int x, int y);
};