#include "WordList.h"

#include <fstream>
#include <iomanip>
#include <sstream>

// removes spaces, tabs and line ending characters from both ends of the text
static std::string Trim(const std::string& text) {
    const char* whitespace = " \t\r\n";
    size_t start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    size_t end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

// returns the first character in the word the font can't draw, or 0 if the font has all of them
static uint32_t FindMissingGlyph(const Font& font, const std::string& word) {
    for (uint32_t codepoint : Utf8ToCodepoints(word)) {
        if (!font.HasGlyph(codepoint)) return codepoint;
    }
    return 0;
}

// writes a codepoint the way unicode charts do eg U+4E2D
static std::string FormatCodepoint(uint32_t codepoint) {
    std::ostringstream stream;
    stream << "U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << codepoint;
    return stream.str();
}

bool LoadWordPairs(const std::string& path, const Font& font, size_t minPairs, std::vector<WordPair>& outPairs, std::string& errorMessage) {
    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Could not open the word list file: " + path;
        return false;
    }

    std::vector<WordPair> pairs;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;

        // windows editors save a UTF-8 byte order mark at the start of the file. skip it
        if (lineNumber == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);

        line = Trim(line);
        if (line.empty()) continue;

        std::string where = path + " line " + std::to_string(lineNumber) + ": ";

        // exactly one comma separates the Maori word from the English word
        size_t comma = line.find(',');
        if (comma == std::string::npos || line.find(',', comma + 1) != std::string::npos) {
            errorMessage = where + "expected 'maori,english' with exactly one comma, but found: " + line;
            return false;
        }

        WordPair pair;
        pair.maori = Trim(line.substr(0, comma));
        pair.english = Trim(line.substr(comma + 1));

        if (pair.maori.empty() || pair.english.empty()) {
            errorMessage = where + "both a Maori and an English word are needed, but found: " + line;
            return false;
        }

        // GetGlyph would draw a '?' for these. a word list with one is a mistake, so refuse to load it
        for (const std::string* word : { &pair.maori, &pair.english }) {
            uint32_t missing = FindMissingGlyph(font, *word);
            if (missing != 0) {
                errorMessage = where + "\"" + *word + "\" has a character the font can't draw (" + FormatCodepoint(missing) + ")";
                return false;
            }
        }

        pairs.push_back(pair);
    }

    if (pairs.empty()) {
        errorMessage = "The word list file has no word pairs in it: " + path;
        return false;
    }

    if (pairs.size() < minPairs) {
        errorMessage = "The word list needs at least " + std::to_string(minPairs) + " word pairs, but " + path + " only has " + std::to_string(pairs.size());
        return false;
    }

    outPairs = std::move(pairs);
    return true;
}
