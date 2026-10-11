#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>

/*
Note to self:

For checking what a font supports: https://fontdrop.info/

REFERENCE FOR CODEPOINTS: https://www.unicode.org/Public/18.0.0/charts/PDF/U0000.pdf
Macron refernce
0x0100  256 Ā
0x0101	257 ā
0x0112	274	Ē
0x0113	275	ē
0x012A	298	Ī
0x012B	299	ī
0x014C	332	Ō
0x014D	333	ō
0x016A	362	Ū
0x016B	363	ū
*/

// everything needed to draw one letter
struct Glyph {
    float x0, y0, x1, y1; // corners of the letters quad in pixels. relative to the pen position (on the baseline). y points down
    float u0, v0, u1, v1; // the letters rectngle inside the atlas image (0 - 1)
    float advance; // how many pixels to move the pen right after drawing this letter
};

class Font {
public:
    // read the .ttf file and bake its letters into an atlas image. returns false on failure
    bool Load(const std::string& ttfPath, float pixelHeight);

    // returns nullptr if the font has no glyph for that unicode codepoint
    const Glyph* GetGlyph(uint32_t codepoint) const;

    // the atlas image. one byte per pixel (0 empty, 255 solid)
    const std::vector<uint8_t>& GetAtlasPixels() const { return atlasPixels; }
    uint32_t GetAtlasWidth() const { return atlasWidth; }
    uint32_t GetAtlasHeight() const { return atlasHeight; }

    // a uv in the middle of a solid white square in the atlas
    // lets us draw plain colored quads (card background) through the same pipeline as text
    glm::vec2 GetWhitePixelUV() const { return whiteUV; }
    // DEBUG: saves the atlas as a png
    bool SaveAtlasPNG(const std::string& path) const;

private:
    uint32_t atlasWidth = 512;
    uint32_t atlasHeight = 100;
    std::vector<uint8_t> atlasPixels;
    std::unordered_map<uint32_t, Glyph> glyphs; // lookup table for chars unicode number to its glyph
    glm::vec2 whiteUV = glm::vec2(0.0f);
};
