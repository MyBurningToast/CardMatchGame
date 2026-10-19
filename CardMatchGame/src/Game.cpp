#include "Game.h"
#include <algorithm>
#include <random>

constexpr int minGridSize = 1; // smallest allowed grid width or height
constexpr int cardsPerPair = 2; // one maori card and one english card

// returns a mutable reference to the card at (x, y)
// the grid is stored as a flat 1D vector in row major order
// so the index is row * width + column
CardData& Game::CardAt(int x, int y) {
    return grid[y * width + x];
}

// read only version of CardAt
const CardData& Game::GetCard(int x, int y) const {
    return grid[y * width + x];
}

// builds and shuffles a new grid of cards from the given word pairs
bool Game::BuildGrid(const std::vector<WordPair>& wordPairs, int gridWidth, int gridHeight, std::string& errorMessage) {
    // a grid with no cards can't be played, and would count as already complete
    if (gridWidth < minGridSize || gridHeight < minGridSize) {
        errorMessage = "Grid size must be at least " + std::to_string(minGridSize) + "x" + std::to_string(minGridSize) + ", but it was " + std::to_string(gridWidth) + "x" + std::to_string(gridHeight);
        return false;
    }

    int cardCount = gridWidth * gridHeight;

    // can't have an odd number of cards
    if (cardCount % cardsPerPair != 0) {
        errorMessage = "A " + std::to_string(gridWidth) + "x" + std::to_string(gridHeight) + " grid has " + std::to_string(cardCount) + " cards. Cards come in pairs, so the total must be even";
        return false;
    }

    int pairsNeeded = cardCount / cardsPerPair;
    if ((int)wordPairs.size() < pairsNeeded) { // not enough words to fill the grid
        errorMessage = "A " + std::to_string(gridWidth) + "x" + std::to_string(gridHeight) + " grid needs " + std::to_string(pairsNeeded) + " word pairs, but only " + std::to_string(wordPairs.size()) + " were given";
        return false;
    }

    width = gridWidth;
    height = gridHeight;

    std::vector<CardData> newCards;
    newCards.reserve(cardCount); // avoid reallocation while pushing


    // for each needed pair, create two cards sharing the same pairId (i),
    // one showing the Maori word and one showing the English word (matching is done by comparing pairIds)
    for (int i = 0; i < pairsNeeded; i++) {
        const WordPair& pair = wordPairs[i];
        newCards.push_back({ i, pair.maori, CardLanguage::Maori });
        newCards.push_back({ i, pair.english, CardLanguage::English });
    }

    std::random_device rd; // OS provided "random"
    std::mt19937 rng(rd()); // Mersenne Twister is a fast pseudorandom number generator, uses the seed from the os
    std::shuffle(newCards.begin(), newCards.end(), rng);

    // move (copy is slow) the finished cards into the real grid
    grid = std::move(newCards);

    // -1 means no card selected
    firstX = noCard; firstY = noCard;
    secondX = noCard; secondY = noCard;
    hasPendingPair = false; // not currently waiting on a match check
    moves = 0;

    return true;
}

FlipResult Game::TryFlipCard(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height)
        return FlipResult::RejectedOutOfBounds;

    // a pair is still waiting to be checked, so reject new flips
    if (hasPendingPair)
        return FlipResult::RejectedPairPending;

    CardData& card = CardAt(x, y);

    // already matched card
    if (card.isMatched)
        return FlipResult::RejectedMatched;

    if (card.isFaceUp)
        return FlipResult::RejectedAlreadyFaceUp;

    card.isFaceUp = true; // valid flip

    // -1 means no card selected (this is the first flip)
    if (firstX == noCard) {
        firstX = x;
        firstY = y;
        return FlipResult::Flipped;
    }

    // otherwise this is the second flip. record it and lock input until the caller checks the pair
    secondX = x;
    secondY = y;
    hasPendingPair = true;
    return FlipResult::FlippedSecond;
}

// Checks the two face up cards and either locks them as matched or flips them back
// true if match, false if not or nothing to check
bool Game::CheckPair() {
    if (!hasPendingPair) return false;
    moves++; // every checked pair counts as a move

    CardData& first = CardAt(firstX, firstY);
    CardData& second = CardAt(secondX, secondY);

    bool isMatch = first.pairId == second.pairId;

    if (isMatch) {
        first.isMatched = true;
        second.isMatched = true;
    }
    else {
        first.isFaceUp = false;
        second.isFaceUp = false;
    }

    // reset back to -1 (none selected)
    firstX = noCard; firstY = noCard;
    secondX = noCard; secondY = noCard;
    hasPendingPair = false;

    return isMatch;
}

bool Game::IsComplete() const {
    for (const auto& card : grid) {
        // a single unmatched card means the game is still going
        if (!card.isMatched) return false;
    }
    return true; // all cards matched
}