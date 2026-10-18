#include "Game.h"
#include <algorithm>
#include <random>

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

// bbuilds and shuffles a new grid of cards from the given word pairs
bool Game::BuildGrid(const std::vector<WordPair>& wordPairs, int gridWidth, int gridHeight) {
    // a grid with no cards can't be played, and would count as already complete
    if (gridWidth <= 0 || gridHeight <= 0) return false;

    int cardCount = gridWidth * gridHeight;

    // cant have an odd number of cards
    if (cardCount % 2 != 0) return false;

    int pairsNeeded = cardCount / 2;
    if ((int)wordPairs.size() < pairsNeeded) return false; // not enough words to fill the grid

    width = gridWidth;
    height = gridHeight;

    std::vector<CardData> newCards;
    newCards.reserve(cardCount); // avoid realocation while pushing


    // for each needed pair, create two cards sharing the same pairId (i),
    // one showing the Maori word and one showing the English word (matching is done by comparing pairIds)
    for (int i = 0; i < pairsNeeded; i++) {
        const WordPair& pair = wordPairs[i];
        newCards.push_back({ i, pair.maori, CardLanguage::Maori });
        newCards.push_back({ i, pair.english, CardLanguage::English });
    }

    std::random_device rd; // OS provided "random"
    std::mt19937 rng(rd()); // Mersenne Twister is a fast pseudorandom number generater, uses the seed from the os
    std::shuffle(newCards.begin(), newCards.end(), rng);

    // move (copy is slow) the finished cards into the real grid
    grid = std::move(newCards);

    // -1 means no card selected
    firstX = -1; firstY = -1;
    secondX = -1; secondY = -1;
    hasPendingPair = false; // not currently waiting on a match check

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
    if (firstX == -1) {
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
    firstX = -1; firstY = -1;
    secondX = -1; secondY = -1;
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
