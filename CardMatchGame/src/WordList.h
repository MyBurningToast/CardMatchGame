#pragma once
#include "Game.h"
#include "Font.h"

#include <string>
#include <vector>

// reads word pairs from a text file. one pair per line, written as: maori,english
// blank lines are ignored
// returns false and fills errorMessage if the file is missing, a line is malformed, there aren't exactly requiredPairs pairs, or a word uses a character the font can't draw
bool LoadWordPairs(const std::string& path, const Font& font, size_t requiredPairs, std::vector<WordPair>& outPairs, std::string& errorMessage);
