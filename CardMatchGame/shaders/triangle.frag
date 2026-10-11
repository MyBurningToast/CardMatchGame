#version 450

layout(location = 0) in vec4 fragColor; // comes from the vertex shaders output
layout(location = 1) in vec2 fragUV; // comes from the vertex shaders output

// the font atlas + sampler bound with the descriptor set (set 0, binding 0)
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 outColor; // written to framebuffer

void main() {
    // the atlas is single channel (R8), so .r is how much ink is at this uv (1.0 = solid, 0.0 = empty)
    // card backgrounds point at the solid white square (1.0) so they come out as their plain color
    // letters point at their glyph, so only the letter shape is visible (the rest is transparent)
    float ink = texture(tex, fragUV).r;

    // the ink amount becomes the alpha. the pipeline's alpha blending mixes it over whats behind
    outColor = vec4(fragColor.rgb, fragColor.a * ink);
}