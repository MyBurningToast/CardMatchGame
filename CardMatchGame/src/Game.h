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
    FlippedSecond, // 2nd card flipped. caller should schedule CheckPair (after a delay)
    RejectedAlreadyFaceUp,
    RejectedMatched,
    RejectedPairPending, // a pair already waiting to be checked
    RejectedOutOfBounds
};

class Game {
public:
    // builds shuffled grid. returns false on failure and puts the reason in errorMessage
    bool BuildGrid(const std::vector<WordPair>& wordPairs, int width, int height, std::string& errorMessage);

    // read only access for rendering. returns const reference
    const CardData& GetCard(int x, int y) const;

    FlipResult TryFlipCard(int x, int y);
    bool CheckPair(); // returns true if it was a match
    bool IsComplete() const;

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    bool HasPendingPair() const { return hasPendingPair; } // TODO: let the UI know when to start the reveal delay
    int GetMoves() const { return moves; } // how many pairs of cards have been flipped since the grid was built

private:
    int width = 0;
    int height = 0;
    std::vector<CardData> grid; // flattened 2D grid. index = y * width + x

    int moves = 0; // one move means flipping two cards and checking them

    int firstX = -1, firstY = -1;
    int secondX = -1, secondY = -1;
    bool hasPendingPair = false;

    // private because only Game should be able to modify cards directly
    CardData& CardAt(int x, int y);
};