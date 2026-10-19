#include "WordList.h"

#include <fstream>
#include <iomanip>
#include <sstream>

const std::string utf8Bom = "\xEF\xBB\xBF"; // windows editors put this at the start of UTF-8 files
static const char pairSeparator = ','; // splits the maori word from the english word
static const int firstLineNumber = 1;
static const uint32_t noMissingGlyph = 0; // FindMissingGlyph returns this when every character can be drawn
const std::string codepointPrefix = "U+";
static const int codepointHexDigits = 4;
static const char codepointPadChar = '0';

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
    return noMissingGlyph;
}

// writes a codepoint the way unicode charts do eg U+4E2D
static std::string FormatCodepoint(uint32_t codepoint) {
    std::ostringstream stream;
    stream << codepointPrefix << std::uppercase << std::hex << std::setw(codepointHexDigits) << std::setfill(codepointPadChar) << codepoint;
    return stream.str();
}

bool LoadWordPairs(const std::string& path, const Font& font, size_t requiredPairs, std::vector<WordPair>& outPairs, std::string& errorMessage) {
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
        if (lineNumber == firstLineNumber && line.rfind(utf8Bom, 0) == 0) line.erase(0, utf8Bom.size());

        line = Trim(line);
        if (line.empty()) continue;

        std::string where = path + " line " + std::to_string(lineNumber) + ": ";

        // exactly one comma separates the Maori word from the English word
        size_t comma = line.find(pairSeparator);
        if (comma == std::string::npos || line.find(pairSeparator, comma + 1) != std::string::npos) {
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
            if (missing != noMissingGlyph) {
                errorMessage = where + "\"" + *word + "\" has a character the font can't draw (" + FormatCodepoint(missing) + ")";
                return false;
            }
        }

        pairs.push_back(pair);
    }

    // the grid needs exactly this many. too few can't fill it, too many would leave words unused
    if (pairs.size() != requiredPairs) {
        errorMessage = "The word list needs exactly " + std::to_string(requiredPairs) + " word pairs, but " + path + " has " + std::to_string(pairs.size());
        return false;
    }

    outPairs = std::move(pairs);
    return true;
}