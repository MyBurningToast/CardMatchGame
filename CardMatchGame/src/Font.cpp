#define STB_TRUETYPE_IMPLEMENTATION // using the code from stb_truetype header file
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "Font.h"

#include <fstream>
#include <iostream>

// converts one of the packed characters into our own Glyph struct and stores it
static void StoreGlyph(std::unordered_map<uint32_t, Glyph>& glyphs, const stbtt_packedchar* chars, int index, uint32_t codepoint, int atlasW, int atlasH) {
    float penX = 0.0f; // start the pen at (0,0) so the quad comes back relative to the pen
    float penY = 0.0f;
    stbtt_aligned_quad quad;
    stbtt_GetPackedQuad(chars, atlasW, atlasH, index, &penX, &penY, &quad, 0);

    Glyph glyph;
    glyph.x0 = quad.x0; glyph.y0 = quad.y0;
    glyph.x1 = quad.x1; glyph.y1 = quad.y1;
    glyph.u0 = quad.s0; glyph.v0 = quad.t0;
    glyph.u1 = quad.s1; glyph.v1 = quad.t1;
    glyph.advance = penX; // GetPackedQuad moved the pen along by this letter's width, so penX is the advance

    glyphs[codepoint] = glyph;
}

bool Font::Load(const std::string& ttfPath, float pixelHeight) {
    // read the whole .ttf file into memory
    std::ifstream file(ttfPath, std::ios::ate | std::ios::binary); // open file. ate means start cursor at the end, binary means no newline translation
    if (!file.is_open()) {
        std::cerr << "Failed to open font file: " << ttfPath << "\n";
        return false;
    }
    size_t fileSize = (size_t)file.tellg(); // cursor is at the end, so its position is file size in bytes
    std::vector<unsigned char> ttfData(fileSize); // buffer sized to hold the whole file
    file.seekg(0); // jump cursor back to the start
    file.read((char*)ttfData.data(), fileSize); // read entire file into the buffer
    file.close(); // release the file handle

    // the atlas starts fully black (0 means no ink)
    atlasPixels.assign((size_t)atlasWidth * atlasHeight, 0);

    // which characters to bake
    // range 1: printable ascii, space (32) up to ~ (126) = 95 characters in a row
    // range 2: the macron vowels. they aren't in a single row, so list
    const int asciiFirst = 32;
    const int asciiCount = 95;
    int macronCodepoints[] = { 0x0100, 0x0101, 0x0112, 0x0113, 0x012A, 0x012B, 0x014C, 0x014D, 0x016A, 0x016B };
    const int macronCount = 10;

    // stb turns the ttf into one glyph atlas bitmap
    // also per glyph lookup data (atlas rect, draw offset, cursor advance)
    // i'm using one vector per codepoint range
    std::vector<stbtt_packedchar> asciiChars(asciiCount);
    std::vector<stbtt_packedchar> macronChars(macronCount);

    stbtt_pack_range ranges[2]{}; // one entry per codepoint range to pack into the atlas
    ranges[0].font_size = pixelHeight;
    ranges[0].first_unicode_codepoint_in_range = asciiFirst; // contiguous range, starts here and goes for num_chars
    ranges[0].array_of_unicode_codepoints = nullptr; // null means using contiguous range above
    ranges[0].num_chars = asciiCount;
    ranges[0].chardata_for_range = asciiChars.data(); // output, stb writes per glyph data here

    ranges[1].font_size = pixelHeight;
    ranges[1].array_of_unicode_codepoints = macronCodepoints; // explicit list instead of a contiguous range
    ranges[1].num_chars = macronCount;
    ranges[1].chardata_for_range = macronChars.data();

    // the bottom few rows are reserved for the white square, so only let stb pack into the rows above them
    // (height is the packing area, stride is still the full width of the atlas)

    const int whiteSize = 4;
    stbtt_pack_context packContext; // stb has a packing state to know where the next glyph goes
    // atlasPixles.data() is atlas bitmap stb writes into
    if (!stbtt_PackBegin(&packContext, atlasPixels.data(), (int)atlasWidth, (int)atlasHeight - whiteSize, (int)atlasWidth, 1, nullptr)) {
        std::cerr << "Failed to start packing the font atlas\n";
        return false;
    }

    // rasterise every glyph in both ranges and pack them into the atlas
    // returns 0 if any glyph didn't fit
    int packed = stbtt_PackFontRanges(&packContext, ttfData.data(), 0, ranges, 2);
    stbtt_PackEnd(&packContext);

    if (!packed) {
        std::cerr << "Failed to pack the font atlas (atlas too small, or the font file is invalid)\n";
        return false;
    }

    // turn stb's packed characters into the glyph structs
    for (int i = 0; i < asciiCount; i++) {
        StoreGlyph(glyphs, asciiChars.data(), i, asciiFirst + i, (int)atlasWidth, (int)atlasHeight);
    }
    for (int i = 0; i < macronCount; i++) {
        StoreGlyph(glyphs, macronChars.data(), i, macronCodepoints[i], (int)atlasWidth, (int)atlasHeight);
    }

    // draw the solid white square in the reserved strip at the bottom left
    for (uint32_t y = atlasHeight - whiteSize; y < atlasHeight; y++) {
        for (uint32_t x = 0; x < (uint32_t)whiteSize; x++) {
            atlasPixels[y * atlasWidth + x] = 255;
        }
    }
    // uv of the centre of the white block at the bottom of the atlas
    // sample here for solid colour quads
    whiteUV = glm::vec2((whiteSize / 2.0f) / atlasWidth, (atlasHeight - whiteSize / 2.0f) / atlasHeight);

    std::cout << "Font loaded: " << glyphs.size() << " glyphs baked\n";
    return true;
}

// look up a character's glyph by codepoint
// a missing letter shows as '?' instead of failing, so one odd character doesn't break the whole card
const Glyph* Font::GetGlyph(uint32_t codepoint) const {
    auto it = glyphs.find(codepoint);
    if (it == glyphs.end()) it = glyphs.find(fallbackCodepoint);
    if (it == glyphs.end()) return nullptr; // '?' is always baked, so this only happens if the font isn't loaded
    return &it->second; // pointer to stored glyph
}

bool Font::HasGlyph(uint32_t codepoint) const {
    return glyphs.find(codepoint) != glyphs.end();
}

bool Font::SaveAtlasPNG(const std::string& path) const {
    // 1 = one channel (grayscale). the last argument is the stride (how many bytes to skip to get from one row to the next)
    if (!stbi_write_png(path.c_str(), (int)atlasWidth, (int)atlasHeight, 1, atlasPixels.data(), (int)atlasWidth)) {
        std::cerr << "Failed to save atlas image: " << path << "\n";
        return false;
    }
    std::cout << "Saved atlas image to " << path << "\n";
    return true;
}

std::vector<uint32_t> Utf8ToCodepoints(const std::string& text) {
    std::vector<uint32_t> codepoints;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        uint32_t codepoint;
        int extraBytes; // how many continuation bytes follow the first byte

        // REFERENCE (byte layout table): https://datatracker.ietf.org/doc/html/rfc3629#section-3

        // the first byte tells us how long the character is
        if (c < 0x80) {
            // 0xxxxxxx: top bit is 0 (anything below 0x80), so it's plain ASCII
            // the byte already is the codepoint, nothing follows
            codepoint = c;
            extraBytes = 0;
        }
        else if ((c >> 5) == 0x6) {
            // 110xxxxx: 2 bytes long (macron vowels are here)
            // c >> 5 drops the 5 data bits, leaving the top 3 bits; 0x6 = 110
            // c & 0x1F: 0x1F = 00011111, keeps the 5 data bits and clears the 110 tag
            codepoint = c & 0x1F;
            extraBytes = 1;
        }
        else if ((c >> 4) == 0xE) {
            // 1110xxxx: 3 bytes long
            // c >> 4 leaves the top 4 bits; 0xE = 1110
            // c & 0x0F: 0x0F = 00001111, keeps the 4 data bits
            codepoint = c & 0x0F;
            extraBytes = 2;
        }
        else if ((c >> 3) == 0x1E) {
            // 11110xxx: 4 bytes long
            // c >> 3 leaves the top 5 bits; 0x1E = 11110
            // c & 0x07: 0x07 = 00000111, keeps the 3 data bits
            codepoint = c & 0x07;
            extraBytes = 3;
        }
        else {
            // not a valid first byte, skip
            i++;
            continue;
        }

        if (i + extraBytes >= text.size()) break; // string ends in the middle of a character

        // each continuation byte is 10xxxxxx. it adds 6 more bits
        for (int k = 1; k <= extraBytes; k++) {
            // shift what we have left by 6 bits
            // 0x3F = 00111111: keep the 6 data bits, drop the 10 tag, and append them
            codepoint = (codepoint << 6) | ((unsigned char)text[i + k] & 0x3F);
        }

        codepoints.push_back(codepoint);
        i += extraBytes + 1;
    }
    return codepoints;
}